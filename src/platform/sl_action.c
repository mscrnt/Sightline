/**
 * sl_action.c - the native action layer: evaluating the binding registry
 * against one poll's devices. See sl_action.h for what an action is and why
 * the layer exists, and sl_bindings.h for the registry itself.
 *
 * THE DEFAULT TABLE, keyboard and mouse (sl_bindings.c g_defaults - the ONE
 * copy; this comment is a reading aid):
 *
 *     W / S / A / D  MOVE FORWARD / BACK / STRAFE LEFT / RIGHT   (#46)
 *     mouse 1 / F    FIRE                                         (#46)
 *     mouse 2 / Q    AIM                                          (#46)
 *     E              INTERACT          use what is in front of Bond
 *     R              RELOAD            reload both hands
 *     Left Ctrl      CROUCH            held = down, released = stand
 *     Left Shift     SPRINT            held (#42); applied by the game only
 *                                      while the Gameplay setting is ON
 *     1 / wheel up   WEAPON_PREVIOUS   in ordinary play
 *     2 / wheel down WEAPON_NEXT       in ordinary play
 *     wheel up       ZOOM_IN           while aiming with an adjustable scope
 *     wheel down     ZOOM_OUT          while aiming with an adjustable scope
 *
 * and the controller's CUSTOM table (live only under BUTTON MODE CUSTOM;
 * under ORIGINAL the caller passes no pad and the pad keeps its control-style
 * path, see sl_input.c read_pad).
 *
 * WHAT MOVED. Until this table existed, E raised the N64 B button (the
 * cartridge's contextual use-or-reload) and R raised A (the style's weapon
 * cycle). Both keys now raise an ACTION instead and produce no N64 button at
 * all; Space still raises B, so the classic contextual button is still on the
 * keyboard for anyone who wants it. With #46 the movement keys and the fire
 * and aim controls moved here too - not to the game (they still reach it as
 * the walk / strafe channels and as N64 Z / R) but out of the hard-coded
 * scancode tests, so the registry is the only thing that says which key
 * walks and which button fires.
 *
 * HELD versus PRESSED. A key or button has a level, and its edge is derived
 * here from the previous poll's level - once per physical press, however many
 * polls it spans. A wheel notch is an EVENT with no level, so it is counted
 * straight into PRESSED. The game side (src/native/sl_action_channels.c)
 * accumulates PRESSED between game ticks, which is what stops an edge being
 * lost when two polls land between two ticks - the same hazard
 * SL_ESC_HOLD_POLLS in sl_input.c exists for, solved by accumulation rather
 * than by holding the press for several polls, because a wheel notch cannot be
 * "held".
 *
 * Nothing here asks the game anything. The wheel context is handed in by the
 * caller; the axis and trigger thresholds are the ones sl_input.c already uses
 * for the same physical controls.
 */
#ifndef __sgi
#include "sl_action.h"
#include "sl_bindings.h"
#include "sl_settings.h"
#include <SDL2/SDL.h>
#include <string.h>

/* Same step point sl_input.c applies to a pad axis standing in for a digital
 * control (SL_PAD_TRIG_ON there, raw SDL units). One value, not two, so a
 * binding on a trigger cannot register at a different pull than the fire and
 * aim triggers do. */
#define SL_ACT_AXIS_ON 8000

static const char *const g_names[SL_ACT_COUNT] = {
    "INTERACT", "RELOAD", "CROUCH", "WEAPON_PREVIOUS", "WEAPON_NEXT",
    "ZOOM_IN", "ZOOM_OUT", "SPRINT",
    "MOVE_FORWARD", "MOVE_BACK", "STRAFE_LEFT", "STRAFE_RIGHT", "FIRE", "AIM",
    "TEXTURE_CYCLE"
};

/* Previous poll's level per action, for the edge. */
static unsigned char g_prev_held[SL_ACT_COUNT];
/* Previous poll's RAW level per slot (the source down, whatever its
 * context), for the stale rule below (round 6). */
static unsigned char g_prev_down[SL_ACT_COUNT][SL_BIND_DEVICES][SL_BIND_SLOTS];

const char *sl_action_name(int action)
{
    if (action < 0 || action >= SL_ACT_COUNT)
        return "?";
    return g_names[action];
}

/* HOLD / TOGGLE (#56): the two actions that have a mode, each with its
 * setting id, its runtime latch, and what the latch was last decided
 * against - the mode and the binding generation - so a change in either
 * drops it. The latch is the ONLY state this adds: the edge it flips on is
 * the evaluator's PRESSED (one per fresh press of the action, however many
 * sources it has, the stale rule included), so no second edge history
 * exists to disagree with the first. */
static const struct {
    int action;
    int setting;
} g_moded[] = {
    { SL_ACT_CROUCH, SL_SET_CROUCH_MODE },
    { SL_ACT_SPRINT, SL_SET_SPRINT_MODE },
};
#define N_MODED ((int) (sizeof g_moded / sizeof g_moded[0]))
static unsigned char g_latch[N_MODED];
static int           g_latch_mode[N_MODED];   /* the mode the latch was decided under */
static unsigned      g_latch_gen[N_MODED];    /* the binding generation, likewise */
static int           g_latch_seen[N_MODED];   /* the two above are meaningful */

void sl_action_latch_reset(void)
{
    memset(g_latch, 0, sizeof g_latch);
}

int sl_action_latched(int action)
{
    int m;
    for (m = 0; m < N_MODED; m++)
        if (g_moded[m].action == action)
            return g_latch[m] != 0;
    return 0;
}

void sl_action_reset(void)
{
    memset(g_prev_held, 0, sizeof g_prev_held);
    memset(g_prev_down, 0, sizeof g_prev_down);
    sl_action_latch_reset();
}

void sl_action_modes_apply(sl_action_state act[SL_ACT_COUNT], int gameplay)
{
    int m;

    for (m = 0; m < N_MODED; m++) {
        int a = g_moded[m].action;
        int mode = sl_settings_get(g_moded[m].setting) == SL_ACTION_MODE_TOGGLE;
        unsigned gen = sl_bindings_generation(a);
        int enabled = 1;

        /* A mode change or a binding change drops the latch. A control held
         * through the change is already down in the evaluator's memory, so
         * it cannot flip the latch back until it is released and pressed
         * again; under HOLD the level simply takes over. */
        if (!g_latch_seen[m] || mode != g_latch_mode[m] || gen != g_latch_gen[m]) {
            g_latch[m] = 0;
            g_latch_mode[m] = mode;
            g_latch_gen[m] = gen;
            g_latch_seen[m] = 1;
        }
        /* SPRINT ENABLED off: nothing to latch, and an old latch does not
         * wait for the setting to come back. The game gates the level the
         * same way (bondview2.c, sl_sprint_enabled), so under HOLD the
         * published level is left as evaluated - the pre-#56 identity. */
        if (a == SL_ACT_SPRINT && sl_settings_get(SL_SET_SPRINT_ENABLED) == 0) {
            g_latch[m] = 0;
            enabled = 0;
        }
        if (mode != SL_ACTION_MODE_TOGGLE)
            continue;                           /* HOLD: as evaluated */
        if (gameplay && enabled && act[a].pressed)
            g_latch[m] ^= 1;
        act[a].held = g_latch[m];
    }
}

/* Is this LEVEL source down in the snapshot? Wheel rows are not levels and
 * return 0 here; they are counted separately below. */
static int source_down(const sl_bind_source *b, const sl_action_devices *dev)
{
    switch (b->kind) {
    case SL_SRC_KEY:
        return dev->keys != NULL && b->code >= 0 && b->code < SDL_NUM_SCANCODES
            && dev->keys[b->code] != 0;
    case SL_SRC_MOUSE_BUTTON:
        return (dev->mouse_buttons & SDL_BUTTON(b->code)) != 0;
    case SL_SRC_PAD_BUTTON:
        return dev->pad != NULL
            && SDL_GameControllerGetButton((SDL_GameController *) dev->pad,
                                           (SDL_GameControllerButton) b->code) != 0;
    case SL_SRC_PAD_AXIS: {
        int v;
        if (dev->pad == NULL)
            return 0;
        v = SDL_GameControllerGetAxis((SDL_GameController *) dev->pad,
                                      (SDL_GameControllerAxis) b->code);
        return b->dir < 0 ? (v < -SL_ACT_AXIS_ON) : (v > SL_ACT_AXIS_ON);
    }
    default:
        return 0;
    }
}

static void add_pressed(sl_action_state *s, int n)
{
    int v = (int) s->pressed + n;
    s->pressed = (unsigned char) (v > 255 ? 255 : v);
}

/* Is this row live in the context the caller says is in force? A row with
 * no context always is; a contextual row (sl_bindings_source_ctx: the wheel
 * or a bumper on a weapon-cycle or zoom action) only in its own context. */
static int row_live(const sl_bind_source *b, int action, const sl_action_devices *dev)
{
    int ctx = sl_bindings_source_ctx(b, action);
    return ctx == SL_CTX_NONE || ctx == dev->wheel_ctx;
}

void sl_action_eval(const sl_action_devices *dev, sl_action_state out[SL_ACT_COUNT])
{
    int a, d, s;
    unsigned char fresh[SL_ACT_COUNT];   /* a live source of the action went down THIS poll */

    sl_bindings_init();                 /* no-op once loaded */
    memset(out, 0, sizeof out[0] * SL_ACT_COUNT);
    memset(fresh, 0, sizeof fresh);

    /* Levels first, over every slot, so an action with two level sources is
     * held while either is down and gets ONE edge for the pair. A contextual
     * row out of its context (a bumper's NEXT WEAPON while the scope is up,
     * its ZOOM IN while it is not - round 6) contributes nothing; its raw
     * state is still remembered, for the stale rule below. */
    for (a = 0; a < SL_ACT_COUNT; a++) {
        for (d = 0; d < SL_BIND_DEVICES; d++) {
            for (s = 0; s < SL_BIND_SLOTS; s++) {
                const sl_bind_source *b = sl_bindings_get(a, d, s);
                int down;
                if (b->kind == SL_SRC_NONE || b->kind == SL_SRC_WHEEL)
                    continue;
                down = source_down(b, dev);
                if (down && row_live(b, a, dev)) {
                    out[a].held = 1;
                    if (d == SL_BIND_PAD) out[a].held_pad = 1;
                    else                  out[a].held_kbm = 1;
                    if (!g_prev_down[a][d][s]) fresh[a] = 1;
                }
                g_prev_down[a][d][s] = (unsigned char) down;
            }
        }
    }
    /* THE EDGE: the level came up this poll - and, the STALE RULE (round
     * 6), it came up because some live source of the action went down this
     * poll, not because a contextual source already held went from the
     * wrong context into the right one. Without it, releasing the scope
     * with RB still down (ZOOM IN, held) would step NEXT WEAPON, and
     * raising it with RB down would pulse a zoom notch. */
    for (a = 0; a < SL_ACT_COUNT; a++) {
        if (out[a].held && !g_prev_held[a] && fresh[a])
            add_pressed(&out[a], 1);
        g_prev_held[a] = out[a].held;
    }

    /* Then the wheel: counted edges, in the context the caller says is in
     * force. A wheel row of the other context contributes nothing this poll
     * (sl_bindings_source_ctx: ZOOM IN / OUT are scoped, the weapon cycle is
     * ordinary play; a wheel row on any other action has no context). */
    for (a = 0; a < SL_ACT_COUNT; a++) {
        for (s = 0; s < SL_BIND_SLOTS; s++) {
            const sl_bind_source *b = sl_bindings_get(a, SL_BIND_KBM, s);
            int n;
            if (b->kind != SL_SRC_WHEEL || !row_live(b, a, dev))
                continue;
            n = (b->dir > 0) ? dev->wheel_up : dev->wheel_down;
            if (n > 0)
                add_pressed(&out[a], n);
        }
    }
}
#endif /* !__sgi */
