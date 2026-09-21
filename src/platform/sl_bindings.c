/**
 * sl_bindings.c - the persistent binding registry (#46). See sl_bindings.h
 * for the contract; this file holds the ONE compiled default table, the live
 * table, the token / name tables, the conflict policy, the persisted form and
 * the capture state machine.
 *
 * THE DEFAULT TABLE is the accepted mapping as of 1ccb8b5c, read off
 * sl_input.c's hard-coded tests and sl_action.c's table before #46 moved
 * them here, with two deliberate notes:
 *
 *   - AIM had THREE keyboard/mouse sources (Q, right button, middle button;
 *     sl_input.c:1072-1073 before #46). Two slots per device is the whole
 *     editor, so the middle button is not a default any more; it is one
 *     capture away for anyone who used it.
 *   - The controller's PAD slots are ALWAYS live (since 2026-09-20 the pad
 *     is only ever the modern dual-stick controller, #63: no BUTTON MODE, no
 *     ORIGINAL profile). Their compiled default IS the DEFAULT button layout
 *     (g_layouts below, the owner's Halo-shaped table; round 6 of the same
 *     day, amended 2026-09-21 by the owner's bumper decision): RT and RB
 *     fire, LT and LB aim, the d-pad up / down zoom in / out and left /
 *     right previous / next weapon, A interact, X reload, B crouch, L3
 *     sprint; Y and R3 unbound; movement has no pad default (the sticks
 *     move through the channels). Before 2026-09-21 the bumpers were the
 *     scope-aware weapon cycle; before 2026-09-20 the compiled pad default
 *     was the #46 CUSTOM layout (RB a second fire, LB a second aim, R3
 *     previous weapon). A slot that equalled an older default had no bind.
 *     line and now reads as the new default, which is the point of the
 *     preset (a file whose lines spell the old table reads CUSTOM and keeps
 *     them) - so a player on DEFAULT with no pad bind. lines gets this
 *     table at the next launch with no manual step, and a player whose file
 *     spells the old one keeps it, as CUSTOM.
 *
 * BUTTON LAYOUTS (#63, 2026-09-20). Named PRESETS of the PAD slots
 * (g_layouts): choosing one writes every action's two PAD slots from the
 * preset and records its id in the store (pad_button_layout); editing any
 * PAD slot afterwards records CUSTOM; RESET DEFAULTS records DEFAULT (the
 * compiled table is the DEFAULT preset, so the two agree by construction -
 * asserted by the tests). At load the recorded preset is checked against the
 * table the bind. lines produced; a mismatch reads as CUSTOM. The presets
 * are DATA - a row per action, two sources each - so renaming or adding one
 * is one table entry; the settings ids are append-only.
 *
 * WHY THE KEY TABLE IS OURS AND NOT SDL_GetScancodeName. The persisted token
 * must be the same string on every machine and every SDL build, and the test
 * harness stubs SDL. A table of the keys a player would bind - letters,
 * digits, F keys, arrows, modifiers, punctuation, the keypad - with a fixed
 * token and a fixed display name is that guarantee, and it is also the
 * CAPTURE FILTER: a key outside it cannot be bound. Excluded on purpose:
 * Escape (cancels a capture; closes menus), Tab (opens the watch), Return /
 * keypad Enter (menu confirm), Space (the fixed classic N64 B, sl_input.c
 * read_keyboard), Backspace and Delete (clear a slot during capture), F8 / F9
 * (the owner's mark keys), and the pad's Start / Back / Guide (pause, the
 * mark). The d-pad's four directions ARE bindable since round 6 (#63 / #64,
 * 2026-09-20, the owner: "D pad isn't used at all, and it should be"): in
 * PLAY they are registry sources like any button (pad:DPAD_UP etc., DEFAULT
 * zoom in / out on up / down and the weapon cycle on left / right); in a
 * MENU sl_input.c still hands them to the game as the N64 d-pad, the menus'
 * fixed navigation, which the registry never sees.
 *
 * CONTEXTUAL SOURCES - the scope-aware weapon cycle (round 6). The wheel has
 * always been contextual: wheel up is PREVIOUS WEAPON (play) AND ZOOM IN
 * (scoped), two rows, and the evaluator counts only the row whose context is
 * in force. That rule is now a property of the SOURCE, not of the wheel
 * kind: g_pad marks the two bumpers `ctx` too, so LB / RB carry the same two
 * rows (DEFAULT: LB previous weapon + zoom out, RB next weapon + zoom in -
 * "it zooms when scoping for the sniper rifle, and cycles weapons when not")
 * and the conflict policy, the evaluator and the label resolver treat them
 * exactly as they treat the wheel. One rule, one flag, no second copy. A
 * source without the flag (every other button, the triggers, the d-pad, the
 * keys) is a level with no context, as before.
 */
#ifndef __sgi
#include "sl_bindings.h"
#include "sl_action.h"
#include "sl_settings.h"
#include "sl_input.h"      /* sl_input_pad_family, the family the labels follow (#63) */
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The pad sl_input.c is reading this poll (its read_pad chose it), so a
 * capture reads the same controller the game does. NULL when none. */
extern void *sl_input_active_pad(void);

/* ------------------------------------------------------------- actions -- */

struct action_meta {
    const char *token;   /* persisted */
    const char *label;   /* the editors */
    const char *brief;   /* the watch's controller page (#63): at most eight
                          * glyphs beside a family's button name, so a line
                          * fits between the page's label column and the pad */
    int         wheel_ctx;
    int         pair;    /* the complementary PAIR this action is half of
                          * (SL_PAIR_*), or 0: a control holding BOTH halves
                          * prints the pair's one word on the watch page
                          * (round 6: the d-pad "WEAPONS/ZOOM") */
};

/* The complementary pairs and their collective briefs (index == id). */
enum { SL_PAIR_NONE = 0, SL_PAIR_WEAPONS, SL_PAIR_ZOOM, SL_PAIR_COUNT };
static const char *const g_pair_brief[SL_PAIR_COUNT] = { NULL, "WEAPONS", "ZOOM" };

/* Index == enum sl_action. */
static const struct action_meta g_actions[SL_ACT_COUNT] = {
    { "interact",        "INTERACT",        "INTERACT", SL_WHEEL_CTX_PLAY,   SL_PAIR_NONE    },
    { "reload",          "RELOAD",          "RELOAD",   SL_WHEEL_CTX_PLAY,   SL_PAIR_NONE    },
    { "crouch",          "CROUCH",          "CROUCH",   SL_WHEEL_CTX_PLAY,   SL_PAIR_NONE    },
    { "weapon_previous", "PREVIOUS WEAPON", "PREV WPN", SL_WHEEL_CTX_PLAY,   SL_PAIR_WEAPONS },
    { "weapon_next",     "NEXT WEAPON",     "NEXT WPN", SL_WHEEL_CTX_PLAY,   SL_PAIR_WEAPONS },
    { "zoom_in",         "ZOOM IN",         "ZOOM IN",  SL_WHEEL_CTX_SCOPED, SL_PAIR_ZOOM    },
    { "zoom_out",        "ZOOM OUT",        "ZOOM OUT", SL_WHEEL_CTX_SCOPED, SL_PAIR_ZOOM    },
    { "sprint",          "SPRINT",          "SPRINT",   SL_WHEEL_CTX_PLAY,   SL_PAIR_NONE    },
    { "move_forward",    "MOVE FORWARD",    "FORWARD",  SL_WHEEL_CTX_PLAY,   SL_PAIR_NONE    },
    { "move_back",       "MOVE BACK",       "BACK",     SL_WHEEL_CTX_PLAY,   SL_PAIR_NONE    },
    { "strafe_left",     "STRAFE LEFT",     "LEFT",     SL_WHEEL_CTX_PLAY,   SL_PAIR_NONE    },
    { "strafe_right",    "STRAFE RIGHT",    "RIGHT",    SL_WHEEL_CTX_PLAY,   SL_PAIR_NONE    },
    { "fire",            "FIRE",            "FIRE",     SL_WHEEL_CTX_PLAY,   SL_PAIR_NONE    },
    { "aim",             "AIM",             "AIM",      SL_WHEEL_CTX_PLAY,   SL_PAIR_NONE    },
};

/* ------------------------------------------------------------ sources -- */

#define KEY(sc, tok, name)  { SL_SRC_KEY, (short) (sc), 0, 0, tok, name }
#define MOUSE(b, tok, name) { SL_SRC_MOUSE_BUTTON, (short) (b), 0, 0, tok, name }
#define WHEEL(d, tok, name) { SL_SRC_WHEEL, 0, (signed char) (d), 1, tok, name }
#define PADB(b, tok, name)  { SL_SRC_PAD_BUTTON, (short) (b), 0, 0, tok, name }
#define PADC(b, tok, name)  { SL_SRC_PAD_BUTTON, (short) (b), 0, 1, tok, name }  /* contextual */
#define PADA(a, tok, name)  { SL_SRC_PAD_AXIS, (short) (a), 1, 0, tok, name }

struct source_meta {
    unsigned char kind;
    short         code;
    signed char   dir;
    unsigned char ctx;     /* a CONTEXTUAL source (the scope-aware weapon
                            * cycle): its rows are live in the wheel context
                            * of their action - see the file header */
    const char   *token;   /* after the "kind:" prefix */
    const char   *name;    /* display */
};

static const struct source_meta g_keys[] = {
    KEY(SDL_SCANCODE_A, "A", "A"), KEY(SDL_SCANCODE_B, "B", "B"),
    KEY(SDL_SCANCODE_C, "C", "C"), KEY(SDL_SCANCODE_D, "D", "D"),
    KEY(SDL_SCANCODE_E, "E", "E"), KEY(SDL_SCANCODE_F, "F", "F"),
    KEY(SDL_SCANCODE_G, "G", "G"), KEY(SDL_SCANCODE_H, "H", "H"),
    KEY(SDL_SCANCODE_I, "I", "I"), KEY(SDL_SCANCODE_J, "J", "J"),
    KEY(SDL_SCANCODE_K, "K", "K"), KEY(SDL_SCANCODE_L, "L", "L"),
    KEY(SDL_SCANCODE_M, "M", "M"), KEY(SDL_SCANCODE_N, "N", "N"),
    KEY(SDL_SCANCODE_O, "O", "O"), KEY(SDL_SCANCODE_P, "P", "P"),
    KEY(SDL_SCANCODE_Q, "Q", "Q"), KEY(SDL_SCANCODE_R, "R", "R"),
    KEY(SDL_SCANCODE_S, "S", "S"), KEY(SDL_SCANCODE_T, "T", "T"),
    KEY(SDL_SCANCODE_U, "U", "U"), KEY(SDL_SCANCODE_V, "V", "V"),
    KEY(SDL_SCANCODE_W, "W", "W"), KEY(SDL_SCANCODE_X, "X", "X"),
    KEY(SDL_SCANCODE_Y, "Y", "Y"), KEY(SDL_SCANCODE_Z, "Z", "Z"),
    KEY(SDL_SCANCODE_1, "1", "1"), KEY(SDL_SCANCODE_2, "2", "2"),
    KEY(SDL_SCANCODE_3, "3", "3"), KEY(SDL_SCANCODE_4, "4", "4"),
    KEY(SDL_SCANCODE_5, "5", "5"), KEY(SDL_SCANCODE_6, "6", "6"),
    KEY(SDL_SCANCODE_7, "7", "7"), KEY(SDL_SCANCODE_8, "8", "8"),
    KEY(SDL_SCANCODE_9, "9", "9"), KEY(SDL_SCANCODE_0, "0", "0"),
    KEY(SDL_SCANCODE_F1, "F1", "F1"), KEY(SDL_SCANCODE_F2, "F2", "F2"),
    KEY(SDL_SCANCODE_F3, "F3", "F3"), KEY(SDL_SCANCODE_F4, "F4", "F4"),
    KEY(SDL_SCANCODE_F5, "F5", "F5"), KEY(SDL_SCANCODE_F6, "F6", "F6"),
    KEY(SDL_SCANCODE_F7, "F7", "F7"),
    KEY(SDL_SCANCODE_F10, "F10", "F10"), KEY(SDL_SCANCODE_F11, "F11", "F11"),
    KEY(SDL_SCANCODE_F12, "F12", "F12"),
    KEY(SDL_SCANCODE_UP, "UP", "UP ARROW"), KEY(SDL_SCANCODE_DOWN, "DOWN", "DOWN ARROW"),
    KEY(SDL_SCANCODE_LEFT, "LEFT", "LEFT ARROW"), KEY(SDL_SCANCODE_RIGHT, "RIGHT", "RIGHT ARROW"),
    KEY(SDL_SCANCODE_LSHIFT, "LSHIFT", "LEFT SHIFT"), KEY(SDL_SCANCODE_RSHIFT, "RSHIFT", "RIGHT SHIFT"),
    KEY(SDL_SCANCODE_LCTRL, "LCTRL", "LEFT CTRL"), KEY(SDL_SCANCODE_RCTRL, "RCTRL", "RIGHT CTRL"),
    KEY(SDL_SCANCODE_LALT, "LALT", "LEFT ALT"), KEY(SDL_SCANCODE_RALT, "RALT", "RIGHT ALT"),
    KEY(SDL_SCANCODE_CAPSLOCK, "CAPSLOCK", "CAPS LOCK"),
    KEY(SDL_SCANCODE_GRAVE, "GRAVE", "GRAVE"), KEY(SDL_SCANCODE_MINUS, "MINUS", "MINUS"),
    KEY(SDL_SCANCODE_EQUALS, "EQUALS", "EQUALS"),
    KEY(SDL_SCANCODE_LEFTBRACKET, "LBRACKET", "LEFT BRACKET"),
    KEY(SDL_SCANCODE_RIGHTBRACKET, "RBRACKET", "RIGHT BRACKET"),
    KEY(SDL_SCANCODE_BACKSLASH, "BACKSLASH", "BACKSLASH"),
    KEY(SDL_SCANCODE_SEMICOLON, "SEMICOLON", "SEMICOLON"),
    KEY(SDL_SCANCODE_APOSTROPHE, "APOSTROPHE", "APOSTROPHE"),
    KEY(SDL_SCANCODE_COMMA, "COMMA", "COMMA"), KEY(SDL_SCANCODE_PERIOD, "PERIOD", "PERIOD"),
    KEY(SDL_SCANCODE_SLASH, "SLASH", "SLASH"),
    KEY(SDL_SCANCODE_INSERT, "INSERT", "INSERT"), KEY(SDL_SCANCODE_HOME, "HOME", "HOME"),
    KEY(SDL_SCANCODE_END, "END", "END"), KEY(SDL_SCANCODE_PAGEUP, "PAGEUP", "PAGE UP"),
    KEY(SDL_SCANCODE_PAGEDOWN, "PAGEDOWN", "PAGE DOWN"),
    KEY(SDL_SCANCODE_KP_0, "KP_0", "KEYPAD 0"), KEY(SDL_SCANCODE_KP_1, "KP_1", "KEYPAD 1"),
    KEY(SDL_SCANCODE_KP_2, "KP_2", "KEYPAD 2"), KEY(SDL_SCANCODE_KP_3, "KP_3", "KEYPAD 3"),
    KEY(SDL_SCANCODE_KP_4, "KP_4", "KEYPAD 4"), KEY(SDL_SCANCODE_KP_5, "KP_5", "KEYPAD 5"),
    KEY(SDL_SCANCODE_KP_6, "KP_6", "KEYPAD 6"), KEY(SDL_SCANCODE_KP_7, "KP_7", "KEYPAD 7"),
    KEY(SDL_SCANCODE_KP_8, "KP_8", "KEYPAD 8"), KEY(SDL_SCANCODE_KP_9, "KP_9", "KEYPAD 9"),
    KEY(SDL_SCANCODE_KP_PLUS, "KP_PLUS", "KEYPAD +"), KEY(SDL_SCANCODE_KP_MINUS, "KP_MINUS", "KEYPAD -"),
    KEY(SDL_SCANCODE_KP_MULTIPLY, "KP_MULTIPLY", "KEYPAD *"),
    KEY(SDL_SCANCODE_KP_DIVIDE, "KP_DIVIDE", "KEYPAD /"),
    KEY(SDL_SCANCODE_KP_PERIOD, "KP_PERIOD", "KEYPAD ."),
};
static const struct source_meta g_mouse[] = {
    MOUSE(SDL_BUTTON_LEFT,   "LEFT",   "MOUSE 1"),
    MOUSE(SDL_BUTTON_RIGHT,  "RIGHT",  "MOUSE 2"),
    MOUSE(SDL_BUTTON_MIDDLE, "MIDDLE", "MOUSE 3"),
    MOUSE(SDL_BUTTON_X1,     "X1",     "MOUSE 4"),
    MOUSE(SDL_BUTTON_X2,     "X2",     "MOUSE 5"),
};
static const struct source_meta g_wheel[] = {
    WHEEL(+1, "UP",   "WHEEL UP"),
    WHEEL(-1, "DOWN", "WHEEL DOWN"),
};
static const struct source_meta g_pad[] = {
    PADB(SDL_CONTROLLER_BUTTON_A,             "A",  "PAD A"),
    PADB(SDL_CONTROLLER_BUTTON_B,             "B",  "PAD B"),
    PADB(SDL_CONTROLLER_BUTTON_X,             "X",  "PAD X"),
    PADB(SDL_CONTROLLER_BUTTON_Y,             "Y",  "PAD Y"),
    PADC(SDL_CONTROLLER_BUTTON_LEFTSHOULDER,  "LB", "PAD LB"),     /* contextual, as the wheel */
    PADC(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, "RB", "PAD RB"),     /* contextual, as the wheel */
    PADB(SDL_CONTROLLER_BUTTON_LEFTSTICK,     "LS", "PAD LS"),
    PADB(SDL_CONTROLLER_BUTTON_RIGHTSTICK,    "RS", "PAD RS"),
    PADA(SDL_CONTROLLER_AXIS_TRIGGERLEFT,     "LT", "PAD LT"),
    PADA(SDL_CONTROLLER_AXIS_TRIGGERRIGHT,    "RT", "PAD RT"),
    /* The d-pad (round 6), appended so the ten indices above - which the
     * family name tables and the capture masks share - do not move. */
    PADB(SDL_CONTROLLER_BUTTON_DPAD_UP,       "DPAD_UP",    "PAD D-PAD UP"),
    PADB(SDL_CONTROLLER_BUTTON_DPAD_DOWN,     "DPAD_DOWN",  "PAD D-PAD DOWN"),
    PADB(SDL_CONTROLLER_BUTTON_DPAD_LEFT,     "DPAD_LEFT",  "PAD D-PAD LEFT"),
    PADB(SDL_CONTROLLER_BUTTON_DPAD_RIGHT,    "DPAD_RIGHT", "PAD D-PAD RIGHT"),
};
#define N(t) ((int) (sizeof t / sizeof t[0]))

/* Same threshold as sl_action.c / sl_input.c for a trigger read as digital. */
#define SL_BIND_TRIG_ON 8000

static const sl_bind_source g_none = { SL_SRC_NONE, 0, 0 };

/* A bounded copy that writes AT MOST n bytes. NOT strncpy: in the native
 * binary that name resolves to the decomp's own src/str.c, whose loop copies
 * the terminator and then pads n - strlen more, i.e. n + 1 bytes for any
 * source shorter than n - Rare's implementation, kept as it is (project
 * rule 5), and one byte past every buffer handed to it. Measured 2026-09-18:
 * strncpy(tok, "none", 32) zeroed the first byte of the key beside it and
 * the slot's line never reached the config. Nothing in src/platform may
 * call strncpy. */
void sl_bind_copy(char *dst, int n, const char *src)
{
    int i = 0;
    if (dst == NULL || n <= 0) return;
    if (src != NULL)
        while (src[i] != '\0' && i < n - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

/* ----------------------------------------------------------- defaults -- */

#define SK(sc)  { SL_SRC_KEY, (short) (sc), 0 }
#define SM(b)   { SL_SRC_MOUSE_BUTTON, (short) (b), 0 }
#define SW(d)   { SL_SRC_WHEEL, 0, (signed char) (d) }
#define SPB(b)  { SL_SRC_PAD_BUTTON, (short) (b), 0 }
#define SPA(a)  { SL_SRC_PAD_AXIS, (short) (a), 1 }
#define S0      { SL_SRC_NONE, 0, 0 }

/* The pad sources by their short names, for the layout tables below. */
#define P_A   SPB(SDL_CONTROLLER_BUTTON_A)
#define P_B   SPB(SDL_CONTROLLER_BUTTON_B)
#define P_X   SPB(SDL_CONTROLLER_BUTTON_X)
#define P_Y   SPB(SDL_CONTROLLER_BUTTON_Y)
#define P_LB  SPB(SDL_CONTROLLER_BUTTON_LEFTSHOULDER)
#define P_RB  SPB(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)
#define P_LS  SPB(SDL_CONTROLLER_BUTTON_LEFTSTICK)
#define P_RS  SPB(SDL_CONTROLLER_BUTTON_RIGHTSTICK)
#define P_LT  SPA(SDL_CONTROLLER_AXIS_TRIGGERLEFT)
#define P_RT  SPA(SDL_CONTROLLER_AXIS_TRIGGERRIGHT)
#define P_DU  SPB(SDL_CONTROLLER_BUTTON_DPAD_UP)
#define P_DD  SPB(SDL_CONTROLLER_BUTTON_DPAD_DOWN)
#define P_DL  SPB(SDL_CONTROLLER_BUTTON_DPAD_LEFT)
#define P_DR  SPB(SDL_CONTROLLER_BUTTON_DPAD_RIGHT)

/* THE BUTTON LAYOUT PRESETS (#63): the PAD slots of every action, in enum
 * sl_action order, two sources each. DEFAULT is ALSO the compiled default
 * table's pad half (g_defaults takes its pad columns from here, so the two
 * cannot drift). The names are what both UIs print; the ids are the store's
 * SL_BUTTON_LAYOUT_* (CUSTOM has no row: it is the state "none of these").
 *
 * ROUND 6 (#63 / #64, 2026-09-20, the owner's replay with both pads: "D pad
 * isn't used at all, and it should be ... To zoom in, we should probably use
 * lb and rb. So it zooms when scoping for sniper rifle, and cycles weapons
 * when not"). The d-pad is bound in every preset - up / down ZOOM IN / OUT,
 * left / right PREVIOUS / NEXT WEAPON - and the bumpers are the scope-aware
 * cycle (contextual sources, see the file header): each holds a weapon-cycle
 * row AND the matching zoom row. Two slots per action is the whole editor,
 * so with the bumper and the d-pad direction in them Y has no NEXT WEAPON
 * slot left and is unbound in every preset (the owner's call which of the
 * three it should be); R3's ZOOM IN went with the zoom to the bumpers and
 * the d-pad (the owner read the R3 label as the stick's own function).
 *
 * THE BUMPERS MIRROR THE TRIGGERS (owner, 2026-09-21: "I also don't like RB
 * and LB and L1 and R1 cycling the weapons. They should just mirror the
 * triggers for aim and shoot. Dpad handles weapons fine."). So RB is a
 * second FIRE and LB a second AIM in every live preset, the weapon cycle and
 * the zoom are the D-PAD's alone, and no preset puts a cycle action on a
 * bumper any more. The scope-aware mechanism itself is UNCHANGED - the `ctx`
 * flag still rides on the wheel and on both bumpers, so a player who binds a
 * cycle or zoom action to a bumper by hand gets the two-row behaviour
 * exactly as before; the defaults simply no longer use it there. FIRE and
 * AIM are not paired actions, so their rows carry no context and a bumper
 * fires and aims in every context, scoped or not (sl_action.c row_live).
 *
 *   DEFAULT       RT / RB fire, LT / LB aim; d-pad up / down zoom in / out,
 *                 left / right previous / next weapon; A interact, X reload,
 *                 B crouch, L3 sprint; Y and R3 unbound
 *   SOUTHPAW      DEFAULT with the two sides swapped: LT / LB fire, RT / RB
 *                 aim; everything else as DEFAULT
 *   BUMPER        RETIRED (see g_layouts): its point was the triggers
 *                 cycling weapons, which is what the decision above removes
 *   GREEN THUMB   DEFAULT with the RIGHT STICK'S CLICK as the second AIM -
 *                 the thumb stays on the stick. Two slots per action is the
 *                 whole editor, so R3 takes the slot LB holds in DEFAULT and
 *                 LB is unbound in THIS preset only; the page says so
 *                 (UNBOUND) rather than hiding it. */
struct sl_pad_layout {
    const char    *name;
    int            retired;   /* kept for its ID only - never offered, never matched */
    sl_bind_source pad[SL_ACT_COUNT][SL_BIND_SLOTS];
};

static const struct sl_pad_layout g_layouts[SL_BUTTON_LAYOUT_PRESETS] = {
    { "DEFAULT", 0, {
        /* INTERACT        */ { P_A,  S0   },
        /* RELOAD          */ { P_X,  S0   },
        /* CROUCH          */ { P_B,  S0   },
        /* WEAPON_PREVIOUS */ { P_DL, S0   },
        /* WEAPON_NEXT     */ { P_DR, S0   },
        /* ZOOM_IN         */ { P_DU, S0   },
        /* ZOOM_OUT        */ { P_DD, S0   },
        /* SPRINT          */ { P_LS, S0   },
        /* MOVE_FORWARD    */ { S0,   S0   },
        /* MOVE_BACK       */ { S0,   S0   },
        /* STRAFE_LEFT     */ { S0,   S0   },
        /* STRAFE_RIGHT    */ { S0,   S0   },
        /* FIRE            */ { P_RT, P_RB },
        /* AIM             */ { P_LT, P_LB } } },
    { "SOUTHPAW", 0, {
        /* INTERACT        */ { P_A,  S0   },
        /* RELOAD          */ { P_X,  S0   },
        /* CROUCH          */ { P_B,  S0   },
        /* WEAPON_PREVIOUS */ { P_DL, S0   },
        /* WEAPON_NEXT     */ { P_DR, S0   },
        /* ZOOM_IN         */ { P_DU, S0   },
        /* ZOOM_OUT        */ { P_DD, S0   },
        /* SPRINT          */ { P_LS, S0   },
        /* MOVE_FORWARD    */ { S0,   S0   },
        /* MOVE_BACK       */ { S0,   S0   },
        /* STRAFE_LEFT     */ { S0,   S0   },
        /* STRAFE_RIGHT    */ { S0,   S0   },
        /* FIRE            */ { P_LT, P_LB },
        /* AIM             */ { P_RT, P_RB } } },
    /* RETIRED (owner, 2026-09-21). BUMPER's whole point was fire and aim on
     * the bumpers with the TRIGGERS cycling weapons, and the triggers
     * cycling is exactly what the decision above removes; with the bumpers
     * now mirroring the triggers in DEFAULT it would differ only in what
     * the owner asked to be rid of. The ID stays occupied because
     * pad_button_layout is persisted and its ids are append-only: the row
     * is never offered (sl_bindings_layout_selectable), never applied
     * (sl_bindings_layout_apply refuses it) and never matched
     * (layout_matches), so a config that recorded BUMPER loads as CUSTOM
     * with its own bind lines untouched - nobody's controls change under
     * them, and choosing a live preset re-seeds as it always did. The rows
     * are kept as written so reviving it is one flag. */
    { "BUMPER", 1, {
        /* INTERACT        */ { P_A,  S0   },
        /* RELOAD          */ { P_X,  S0   },
        /* CROUCH          */ { P_B,  S0   },
        /* WEAPON_PREVIOUS */ { P_LT, P_DL },
        /* WEAPON_NEXT     */ { P_RT, P_DR },
        /* ZOOM_IN         */ { P_DU, S0   },
        /* ZOOM_OUT        */ { P_DD, S0   },
        /* SPRINT          */ { P_LS, S0   },
        /* MOVE_FORWARD    */ { S0,   S0   },
        /* MOVE_BACK       */ { S0,   S0   },
        /* STRAFE_LEFT     */ { S0,   S0   },
        /* STRAFE_RIGHT    */ { S0,   S0   },
        /* FIRE            */ { P_RB, S0   },
        /* AIM             */ { P_LB, S0   } } },
    { "GREEN THUMB", 0, {
        /* INTERACT        */ { P_A,  S0   },
        /* RELOAD          */ { P_X,  S0   },
        /* CROUCH          */ { P_B,  S0   },
        /* WEAPON_PREVIOUS */ { P_DL, S0   },
        /* WEAPON_NEXT     */ { P_DR, S0   },
        /* ZOOM_IN         */ { P_DU, S0   },
        /* ZOOM_OUT        */ { P_DD, S0   },
        /* SPRINT          */ { P_LS, S0   },
        /* MOVE_FORWARD    */ { S0,   S0   },
        /* MOVE_BACK       */ { S0,   S0   },
        /* STRAFE_LEFT     */ { S0,   S0   },
        /* STRAFE_RIGHT    */ { S0,   S0   },
        /* FIRE            */ { P_RT, P_RB },
        /* AIM             */ { P_LT, P_RS } } },
};

/* THE ONE COMPILED DEFAULT TABLE. [action][device][slot]. The KBM half is
 * the accepted #46 table; the PAD half is the DEFAULT layout above, copied
 * in at the first init (defaults_ready) so the two are one table. */
#define KD(k1, k2) { { k1, k2 }, { S0, S0 } }
static sl_bind_source g_defaults[SL_ACT_COUNT][SL_BIND_DEVICES][SL_BIND_SLOTS] = {
    /* INTERACT        */ KD(SK(SDL_SCANCODE_E),      S0),
    /* RELOAD          */ KD(SK(SDL_SCANCODE_R),      S0),
    /* CROUCH          */ KD(SK(SDL_SCANCODE_LCTRL),  S0),
    /* WEAPON_PREVIOUS */ KD(SK(SDL_SCANCODE_1),      SW(+1)),
    /* WEAPON_NEXT     */ KD(SK(SDL_SCANCODE_2),      SW(-1)),
    /* ZOOM_IN         */ KD(SW(+1),                  S0),
    /* ZOOM_OUT        */ KD(SW(-1),                  S0),
    /* SPRINT          */ KD(SK(SDL_SCANCODE_LSHIFT), S0),
    /* MOVE_FORWARD    */ KD(SK(SDL_SCANCODE_W),      S0),
    /* MOVE_BACK       */ KD(SK(SDL_SCANCODE_S),      S0),
    /* STRAFE_LEFT     */ KD(SK(SDL_SCANCODE_A),      S0),
    /* STRAFE_RIGHT    */ KD(SK(SDL_SCANCODE_D),      S0),
    /* FIRE            */ KD(SM(SDL_BUTTON_LEFT),     SK(SDL_SCANCODE_F)),
    /* AIM             */ KD(SM(SDL_BUTTON_RIGHT),    SK(SDL_SCANCODE_Q)),
};
static int g_defaults_ready;

static void defaults_ready(void)
{
    int a, s;
    if (g_defaults_ready) return;
    g_defaults_ready = 1;
    for (a = 0; a < SL_ACT_COUNT; a++)
        for (s = 0; s < SL_BIND_SLOTS; s++)
            g_defaults[a][SL_BIND_PAD][s] = g_layouts[SL_BUTTON_LAYOUT_DEFAULT].pad[a][s];
}

static sl_bind_source g_table[SL_ACT_COUNT][SL_BIND_DEVICES][SL_BIND_SLOTS];
static int g_loaded;
static int g_applying_layout;     /* a preset is being written: not a CUSTOM edit */
/* Per-action binding generation (#56, sl_bindings_generation): advanced by
 * every slot write (slot_persist), by RESET DEFAULTS and by a (re)load. */
static unsigned g_gen[SL_ACT_COUNT];

unsigned sl_bindings_generation(int action)
{
    return (action >= 0 && action < SL_ACT_COUNT) ? g_gen[action] : 0u;
}

static void gen_bump_all(void)
{
    int a;
    for (a = 0; a < SL_ACT_COUNT; a++)
        g_gen[a]++;
}

/* --------------------------------------------------------- name lookup -- */

static const struct source_meta *find_meta(const sl_bind_source *s)
{
    const struct source_meta *t;
    int i, n;

    switch (s->kind) {
    case SL_SRC_KEY:          t = g_keys;  n = N(g_keys);  break;
    case SL_SRC_MOUSE_BUTTON: t = g_mouse; n = N(g_mouse); break;
    case SL_SRC_WHEEL:        t = g_wheel; n = N(g_wheel); break;
    case SL_SRC_PAD_BUTTON:
    case SL_SRC_PAD_AXIS:     t = g_pad;   n = N(g_pad);   break;
    default:                  return NULL;
    }
    for (i = 0; i < n; i++) {
        if (t[i].kind != s->kind) continue;
        if (s->kind == SL_SRC_WHEEL) { if (t[i].dir == s->dir) return &t[i]; }
        else if (t[i].code == s->code) return &t[i];
    }
    return NULL;
}

static const char *kind_prefix(int kind)
{
    switch (kind) {
    case SL_SRC_KEY:          return "key";
    case SL_SRC_MOUSE_BUTTON: return "mouse";
    case SL_SRC_WHEEL:        return "wheel";
    case SL_SRC_PAD_BUTTON:
    case SL_SRC_PAD_AXIS:     return "pad";
    default:                  return "none";
    }
}

const char *sl_bindings_source_token(const sl_bind_source *src, char *buf, int n)
{
    const struct source_meta *m = src ? find_meta(src) : NULL;
    if (buf == NULL || n <= 0) return "";
    if (m == NULL) { sl_bind_copy(buf, n, "none"); return buf; }
    snprintf(buf, (size_t) n, "%s:%s", kind_prefix(m->kind), m->token);
    return buf;
}

/* FAMILY LABELS (#63). The persisted token is device-neutral SDL-canonical
 * ("pad:A" is SDL's south face button whatever pad is plugged in, so a file
 * written with an Xbox pad reads unchanged with a DualSense) and only the
 * DISPLAY name follows the attached family: an Xbox-family pad prints A / B
 * / X / Y / LB / RB / LT / RT / LS / RS, a PlayStation-family pad CROSS /
 * CIRCLE / SQUARE / TRIANGLE / L1 / R1 / L2 / R2 / L3 / R3, and anything else
 * (or no pad) the neutral "PAD A" the editors printed before. Indexed in
 * g_pad's order; the test suite asserts the three tables stay parallel. */
static const char *const g_pad_name_xbox[] = {
    "A", "B", "X", "Y", "LB", "RB", "LS", "RS", "LT", "RT",
    "D-PAD UP", "D-PAD DOWN", "D-PAD LEFT", "D-PAD RIGHT"
};
static const char *const g_pad_name_ps[] = {
    "CROSS", "CIRCLE", "SQUARE", "TRIANGLE", "L1", "R1", "L3", "R3", "L2", "R2",
    "D-PAD UP", "D-PAD DOWN", "D-PAD LEFT", "D-PAD RIGHT"
};

const char *sl_bindings_source_label(const sl_bind_source *src, int family,
                                     char *buf, int n)
{
    const struct source_meta *m = src ? find_meta(src) : NULL;
    if (buf == NULL || n <= 0) return "";
    if (m != NULL && (m->kind == SL_SRC_PAD_BUTTON || m->kind == SL_SRC_PAD_AXIS)
        && m >= g_pad && m < g_pad + N(g_pad)) {
        int i = (int) (m - g_pad);
        if (family == SL_PAD_FAMILY_XBOX) {
            sl_bind_copy(buf, n, g_pad_name_xbox[i]);
            return buf;
        }
        if (family == SL_PAD_FAMILY_PLAYSTATION) {
            sl_bind_copy(buf, n, g_pad_name_ps[i]);
            return buf;
        }
    }
    sl_bind_copy(buf, n, m ? m->name : "---");
    return buf;
}

const char *sl_bindings_source_name(const sl_bind_source *src, char *buf, int n)
{
    return sl_bindings_source_label(src, sl_input_pad_family(), buf, n);
}

int sl_bindings_source_parse(const char *token, sl_bind_source *out)
{
    const struct source_meta *t;
    const char *colon, *rest;
    int i, n;
    size_t plen;

    if (token == NULL || out == NULL) return 0;
    if (strcmp(token, "none") == 0) { *out = g_none; return 1; }
    colon = strchr(token, ':');
    if (colon == NULL) return 0;
    plen = (size_t) (colon - token);
    rest = colon + 1;
    if (plen == 3 && strncmp(token, "key", 3) == 0)        { t = g_keys;  n = N(g_keys); }
    else if (plen == 5 && strncmp(token, "mouse", 5) == 0) { t = g_mouse; n = N(g_mouse); }
    else if (plen == 5 && strncmp(token, "wheel", 5) == 0) { t = g_wheel; n = N(g_wheel); }
    else if (plen == 3 && strncmp(token, "pad", 3) == 0)   { t = g_pad;   n = N(g_pad); }
    else return 0;
    for (i = 0; i < n; i++) {
        if (strcmp(t[i].token, rest) == 0) {
            out->kind = t[i].kind;
            out->code = t[i].code;
            out->dir  = t[i].dir;
            return 1;
        }
    }
    return 0;
}

int sl_bindings_source_device(const sl_bind_source *src)
{
    if (src == NULL) return -1;
    switch (src->kind) {
    case SL_SRC_KEY: case SL_SRC_MOUSE_BUTTON: case SL_SRC_WHEEL: return SL_BIND_KBM;
    case SL_SRC_PAD_BUTTON: case SL_SRC_PAD_AXIS:                 return SL_BIND_PAD;
    default: return -1;
    }
}

int sl_bindings_source_capturable(const sl_bind_source *src)
{
    return src != NULL && find_meta(src) != NULL;
}

int sl_bindings_source_equal(const sl_bind_source *a, const sl_bind_source *b)
{
    if (a == NULL || b == NULL) return 0;
    if (a->kind != b->kind) return 0;
    if (a->kind == SL_SRC_NONE) return 1;
    if (a->kind == SL_SRC_WHEEL) return a->dir == b->dir;
    if (a->kind == SL_SRC_PAD_AXIS) return a->code == b->code && a->dir == b->dir;
    return a->code == b->code;
}

const char *sl_bindings_action_token(int action)
{
    return (action >= 0 && action < SL_ACT_COUNT) ? g_actions[action].token : NULL;
}

const char *sl_bindings_action_label(int action)
{
    return (action >= 0 && action < SL_ACT_COUNT) ? g_actions[action].label : "?";
}

const char *sl_bindings_action_brief(int action)
{
    return (action >= 0 && action < SL_ACT_COUNT) ? g_actions[action].brief : "?";
}

int sl_bindings_action_from_token(const char *token)
{
    int a;
    if (token == NULL) return -1;
    for (a = 0; a < SL_ACT_COUNT; a++)
        if (strcmp(g_actions[a].token, token) == 0) return a;
    return -1;
}

int sl_bindings_wheel_ctx(int action)
{
    return (action >= 0 && action < SL_ACT_COUNT) ? g_actions[action].wheel_ctx
                                                  : SL_WHEEL_CTX_PLAY;
}

/* THE ONE CONTEXT RULE (round 6). A row = (source, action). It carries a
 * context when the source is contextual (the wheel, the two bumpers) AND
 * the action is half of a complementary pair - a weapon-cycle row is live
 * in ordinary play, a zoom row while aiming with an adjustable scope - and
 * that is the whole of the scope-aware weapon cycle: the evaluator skips a
 * row whose context is not in force, the conflict policy lets two rows of
 * the same source coexist when their contexts differ. Every other row is
 * a level (or, for the wheel, an event) with no context: a bumper on AIM
 * under BUMPER is held straight through the scope it opened, and a wheel
 * notch bound to INTERACT interacts in either context. */
int sl_bindings_source_ctx(const sl_bind_source *src, int action)
{
    const struct source_meta *m = src ? find_meta(src) : NULL;
    if (m == NULL || !m->ctx) return SL_CTX_NONE;
    if (action < 0 || action >= SL_ACT_COUNT || g_actions[action].pair == SL_PAIR_NONE)
        return SL_CTX_NONE;
    return g_actions[action].wheel_ctx;
}

const char *sl_bindings_pair_brief(int action_a, int action_b)
{
    int pa, pb;
    if (action_a < 0 || action_a >= SL_ACT_COUNT || action_b < 0 || action_b >= SL_ACT_COUNT)
        return NULL;
    if (action_a == action_b) return NULL;
    pa = g_actions[action_a].pair; pb = g_actions[action_b].pair;
    if (pa == SL_PAIR_NONE || pa != pb) return NULL;
    return g_pair_brief[pa];
}

/* --------------------------------------------------------- persistence -- */

static int slot_ok(int action, int device, int slot)
{
    return action >= 0 && action < SL_ACT_COUNT
        && device >= 0 && device < SL_BIND_DEVICES
        && slot >= 0 && slot < SL_BIND_SLOTS;
}

static void slot_key(int action, int device, int slot, char *buf, int n)
{
    snprintf(buf, (size_t) n, "%s%s.%s.%d", SL_SETTINGS_EXT_PREFIX,
             g_actions[action].token, device == SL_BIND_PAD ? "pad" : "kbm",
             slot + 1);
}

/* Persist one slot: at its default the line goes away, otherwise the token
 * ("none" for an emptied slot with a non-empty default). */
static void slot_persist(int action, int device, int slot)
{
    char key[SL_SETTINGS_EXT_KEY], tok[SL_SETTINGS_EXT_VAL];
    int is_default, changed;
    g_gen[action]++;                  /* #56: this action's sources changed */
    slot_key(action, device, slot, key, (int) sizeof key);
    is_default = sl_bindings_source_equal(&g_table[action][device][slot],
                                          &g_defaults[action][device][slot]);
    if (is_default) {
        changed = sl_settings_ext_set(key, NULL);
    } else {
        sl_bindings_source_token(&g_table[action][device][slot], tok, (int) sizeof tok);
        changed = sl_settings_ext_set(key, tok);
    }
    if (getenv("SL_INPUT_DEBUG") != NULL)
        fprintf(stderr, "sightline bindings: persist %s=%s (%s, %d line(s) held)\n",
                key, is_default ? "<default, line removed>" : tok,
                changed ? "stored" : "unchanged", sl_settings_ext_count());
}

void sl_bindings_reload(void)
{
    g_loaded = 0;
    sl_bindings_capture_cancel();
    sl_bindings_init();
}

/* Does the live PAD table equal a preset's rows exactly? A RETIRED preset
 * never matches, so a table that still spells its rows (a config written
 * before it was retired) reads as CUSTOM and keeps those rows. */
static int layout_matches(int layout)
{
    int a, s;
    if (layout < 0 || layout >= SL_BUTTON_LAYOUT_PRESETS) return 0;
    if (g_layouts[layout].retired) return 0;
    for (a = 0; a < SL_ACT_COUNT; a++)
        for (s = 0; s < SL_BIND_SLOTS; s++)
            if (!sl_bindings_source_equal(&g_table[a][SL_BIND_PAD][s],
                                          &g_layouts[layout].pad[a][s]))
                return 0;
    return 1;
}

void sl_bindings_init(void)
{
    int a, d, s, overrides = 0, bad = 0, layout;

    if (g_loaded) return;
    g_loaded = 1;
    defaults_ready();
    memcpy(g_table, g_defaults, sizeof g_table);
    gen_bump_all();                   /* #56: every action's sources (re)loaded */

    for (a = 0; a < SL_ACT_COUNT; a++) {
        for (d = 0; d < SL_BIND_DEVICES; d++) {
            for (s = 0; s < SL_BIND_SLOTS; s++) {
                char key[SL_SETTINGS_EXT_KEY];
                const char *v;
                sl_bind_source src;
                slot_key(a, d, s, key, (int) sizeof key);
                v = sl_settings_ext_get(key);
                if (v == NULL) continue;
                /* A malformed token, or one of the wrong device class for
                 * the slot, leaves THIS slot at its default. */
                if (!sl_bindings_source_parse(v, &src)
                    || (src.kind != SL_SRC_NONE && sl_bindings_source_device(&src) != d)) {
                    bad++;
                    continue;
                }
                g_table[a][d][s] = src;
                overrides++;
            }
        }
    }
    /* THE RECORDED BUTTON LAYOUT against the table the lines produced: a
     * preset whose rows no longer match (a hand-edited config, a line from
     * an older build's defaults) reads as CUSTOM, so the row never claims a
     * preset the pad does not have. Write-on-change: an honest file is not
     * touched. */
    layout = sl_settings_get(SL_SET_PAD_BUTTON_LAYOUT);
    if (layout != SL_BUTTON_LAYOUT_CUSTOM && !layout_matches(layout))
        sl_settings_set(SL_SET_PAD_BUTTON_LAYOUT, SL_BUTTON_LAYOUT_CUSTOM);
    if (sl_settings_active() || overrides || bad)
        fprintf(stderr, "sightline bindings: loaded (%d override(s), %d malformed ignored, button layout %s)\n",
                overrides, bad, sl_bindings_layout_name(sl_settings_get(SL_SET_PAD_BUTTON_LAYOUT)));
    if (getenv("SL_INPUT_DEBUG") != NULL)
        sl_bindings_dump();
}

const sl_bind_source *sl_bindings_get(int action, int device, int slot)
{
    sl_bindings_init();
    if (!slot_ok(action, device, slot)) return &g_none;
    return &g_table[action][device][slot];
}

const sl_bind_source *sl_bindings_default(int action, int device, int slot)
{
    defaults_ready();
    if (!slot_ok(action, device, slot)) return &g_none;
    return &g_defaults[action][device][slot];
}

/* ------------------------------------------------------ button layouts -- */

const char *sl_bindings_layout_name(int layout)
{
    if (layout >= 0 && layout < SL_BUTTON_LAYOUT_PRESETS) return g_layouts[layout].name;
    if (layout == SL_BUTTON_LAYOUT_CUSTOM) return "CUSTOM";
    return "?";
}

int sl_bindings_layout_get(void)
{
    int layout;
    sl_bindings_init();
    layout = sl_settings_get(SL_SET_PAD_BUTTON_LAYOUT);
    return (layout >= 0 && layout < SL_BUTTON_LAYOUT_COUNT) ? layout : SL_BUTTON_LAYOUT_CUSTOM;
}

const sl_bind_source *sl_bindings_layout_source(int layout, int action, int slot)
{
    if (layout < 0 || layout >= SL_BUTTON_LAYOUT_PRESETS || !slot_ok(action, SL_BIND_PAD, slot))
        return &g_none;
    return &g_layouts[layout].pad[action][slot];
}

int sl_bindings_layout_selectable(int layout)
{
    return layout >= 0 && layout < SL_BUTTON_LAYOUT_PRESETS && !g_layouts[layout].retired;
}

/* The next SELECTABLE preset from `cur` in direction `dir`, wrapping; a step
 * off CUSTOM (or off a retired id a config recorded) lands on the first
 * preset going up and the last going down. The ONE place the preset order
 * is walked - both editors step through sl_watch_layout_step, which calls
 * this - so retiring a preset is the one `retired` flag and nothing else. */
int sl_bindings_layout_step(int cur, int dir)
{
    int n = SL_BUTTON_LAYOUT_PRESETS, i, l;
    if (dir == 0) return cur;
    if (!sl_bindings_layout_selectable(cur)) {
        l = dir > 0 ? 0 : n - 1;
        for (i = 0; i < n; i++, l = (l + (dir > 0 ? 1 : n - 1)) % n)
            if (sl_bindings_layout_selectable(l)) return l;
        return cur;
    }
    l = cur;
    for (i = 0; i < n; i++) {
        l = (l + (dir > 0 ? 1 : n - 1)) % n;
        if (sl_bindings_layout_selectable(l)) return l;
    }
    return cur;
}

int sl_bindings_layout_apply(int layout)
{
    int a, s, changed = 0;

    sl_bindings_init();
    if (!sl_bindings_layout_selectable(layout)) return 0;
    sl_settings_batch_begin();
    g_applying_layout = 1;
    /* Every PAD slot of every action, from the preset: a plain write of the
     * whole column (no steal logic - a preset is consistent by construction,
     * and the KBM column is never touched). */
    for (a = 0; a < SL_ACT_COUNT; a++) {
        for (s = 0; s < SL_BIND_SLOTS; s++) {
            const sl_bind_source *want = &g_layouts[layout].pad[a][s];
            if (sl_bindings_source_equal(&g_table[a][SL_BIND_PAD][s], want)) continue;
            g_table[a][SL_BIND_PAD][s] = *want;
            slot_persist(a, SL_BIND_PAD, s);
            changed = 1;
        }
    }
    if (sl_settings_get(SL_SET_PAD_BUTTON_LAYOUT) != layout) {
        sl_settings_set(SL_SET_PAD_BUTTON_LAYOUT, layout);
        changed = 1;
    }
    g_applying_layout = 0;
    sl_settings_batch_end();
    if (getenv("SL_INPUT_DEBUG") != NULL)
        fprintf(stderr, "sightline bindings: button layout %s applied (%s)\n",
                g_layouts[layout].name, changed ? "pad slots written" : "already in force");
    return changed;
}

int sl_bindings_actions_for_source(const sl_bind_source *src, int device, int *actions, int max)
{
    int a, s, n = 0;
    sl_bindings_init();
    if (src == NULL || src->kind == SL_SRC_NONE || device < 0 || device >= SL_BIND_DEVICES)
        return 0;
    for (a = 0; a < SL_ACT_COUNT; a++) {
        for (s = 0; s < SL_BIND_SLOTS; s++) {
            if (!sl_bindings_source_equal(&g_table[a][device][s], src)) continue;
            if (actions != NULL && n < max) actions[n] = a;
            n++;
            break;                      /* an action counts once, whichever slot */
        }
    }
    return n;
}

int sl_bindings_is_default(int action, int device, int slot)
{
    sl_bindings_init();
    if (!slot_ok(action, device, slot)) return 1;
    return sl_bindings_source_equal(&g_table[action][device][slot],
                                    &g_defaults[action][device][slot]);
}

/* Two sources conflict when they are the same physical control in the same
 * context: a row with no context conflicts with any use of its source; two
 * rows of a contextual source coexist only when both carry a context and
 * the contexts differ (sl_bindings_source_ctx - wheel up as PREVIOUS WEAPON
 * and ZOOM IN, RB as NEXT WEAPON and ZOOM IN). */
static int conflicts(int action_a, const sl_bind_source *a,
                     int action_b, const sl_bind_source *b)
{
    int ca, cb;
    if (!sl_bindings_source_equal(a, b) || a->kind == SL_SRC_NONE) return 0;
    ca = sl_bindings_source_ctx(a, action_a);
    cb = sl_bindings_source_ctx(b, action_b);
    if (ca != SL_CTX_NONE && cb != SL_CTX_NONE && ca != cb) return 0;
    return 1;
}

int sl_bindings_set(int action, int device, int slot, const sl_bind_source *src,
                    int *stolen_from)
{
    sl_bind_source want;
    int a, d, s, changed = 0;

    sl_bindings_init();
    if (stolen_from) *stolen_from = -1;
    if (!slot_ok(action, device, slot)) return 0;
    want = src ? *src : g_none;
    if (want.kind != SL_SRC_NONE) {
        if (!sl_bindings_source_capturable(&want)) return 0;
        if (sl_bindings_source_device(&want) != device) return 0;
    }
    if (sl_bindings_source_equal(&g_table[action][device][slot], &want))
        return 0;

    sl_settings_batch_begin();
    /* Steal: every other slot holding this source in the same context is
     * emptied first (the same action's other slot included - a source in
     * both slots of one action would be pointless). */
    if (want.kind != SL_SRC_NONE) {
        for (a = 0; a < SL_ACT_COUNT; a++) {
            for (d = 0; d < SL_BIND_DEVICES; d++) {
                for (s = 0; s < SL_BIND_SLOTS; s++) {
                    if (a == action && d == device && s == slot) continue;
                    if (!conflicts(action, &want, a, &g_table[a][d][s])) continue;
                    g_table[a][d][s] = g_none;
                    slot_persist(a, d, s);
                    if (stolen_from && a != action) *stolen_from = a;
                    changed = 1;
                }
            }
        }
    }
    g_table[action][device][slot] = want;
    slot_persist(action, device, slot);
    changed = 1;
    /* A PAD slot edited by hand (an editor's capture or clear; a steal only
     * ever empties a slot of the same device class, so a KBM edit cannot
     * touch the pad column): the button layout is now CUSTOM - unless the
     * pad table happens to be a preset again, in which case that preset.
     * Never while a preset itself is being written. */
    if (!g_applying_layout && device == SL_BIND_PAD) {
        int l, found = SL_BUTTON_LAYOUT_CUSTOM;
        for (l = 0; l < SL_BUTTON_LAYOUT_PRESETS; l++)
            if (layout_matches(l)) { found = l; break; }
        sl_settings_set(SL_SET_PAD_BUTTON_LAYOUT, found);
    }
    sl_settings_batch_end();

    if (getenv("SL_INPUT_DEBUG") != NULL) {
        char tok[SL_SETTINGS_EXT_VAL];
        fprintf(stderr, "sightline bindings: %s %s %d = %s%s%s\n",
                g_actions[action].token, device == SL_BIND_PAD ? "pad" : "kbm", slot + 1,
                sl_bindings_source_token(&want, tok, (int) sizeof tok),
                (stolen_from && *stolen_from >= 0) ? " (taken from " : "",
                (stolen_from && *stolen_from >= 0) ? g_actions[*stolen_from].token : "");
        if (stolen_from && *stolen_from >= 0) fprintf(stderr, ")\n");
    }
    return changed;
}

void sl_bindings_reset_defaults(void)
{
    sl_bindings_init();
    memcpy(g_table, g_defaults, sizeof g_table);
    gen_bump_all();                   /* #56: every action's sources changed */
    sl_settings_batch_begin();
    (void) sl_settings_ext_remove_prefix(SL_SETTINGS_EXT_PREFIX);
    /* The compiled pad table IS the DEFAULT preset: the layout row says so. */
    sl_settings_set(SL_SET_PAD_BUTTON_LAYOUT, SL_BUTTON_LAYOUT_DEFAULT);
    sl_settings_batch_end();
    if (getenv("SL_INPUT_DEBUG") != NULL)
        fprintf(stderr, "sightline bindings: reset to defaults\n");
}

void sl_bindings_dump(void)
{
    int a, d, s;
    char tok[SL_SETTINGS_EXT_VAL];
    sl_bindings_init();
    for (a = 0; a < SL_ACT_COUNT; a++) {
        fprintf(stderr, "sightline bindings: %-16s", g_actions[a].token);
        for (d = 0; d < SL_BIND_DEVICES; d++)
            for (s = 0; s < SL_BIND_SLOTS; s++)
                fprintf(stderr, " %s", sl_bindings_source_token(&g_table[a][d][s], tok, (int) sizeof tok));
        fprintf(stderr, "\n");
    }
}

/* -------------------------------------------------------------- capture -- */

/* One capture: the target slot, and the set of sources that were down when
 * it began (ignored until each has been seen released). */
static struct {
    int active;
    int action, device, slot;
    unsigned char key_held[SDL_NUM_SCANCODES];
    unsigned      mouse_held;
    unsigned      padb_held;          /* bit per SDL_GameControllerButton */
    unsigned      pada_held;          /* bit per trigger axis */
    int stolen_from;
    /* After completion: the captured source, held until released, keeps
     * the input path neutral (sl_bindings_capture_blocking). */
    int           done_holding;
    sl_bind_source done_src;
    /* Backspace / Delete "clean" = not held at begin. */
} g_cap;

static unsigned pad_button_mask(SDL_GameController *c)
{
    unsigned m = 0;
    int i;
    if (c == NULL) return 0;
    for (i = 0; i < N(g_pad); i++) {
        if (g_pad[i].kind != SL_SRC_PAD_BUTTON) continue;
        if (SDL_GameControllerGetButton(c, (SDL_GameControllerButton) g_pad[i].code))
            m |= 1u << i;
    }
    return m;
}

static unsigned pad_axis_mask(SDL_GameController *c)
{
    unsigned m = 0;
    int i;
    if (c == NULL) return 0;
    for (i = 0; i < N(g_pad); i++) {
        if (g_pad[i].kind != SL_SRC_PAD_AXIS) continue;
        if (SDL_GameControllerGetAxis(c, (SDL_GameControllerAxis) g_pad[i].code) > SL_BIND_TRIG_ON)
            m |= 1u << i;
    }
    return m;
}

void sl_bindings_capture_begin(int action, int device, int slot)
{
    const unsigned char *k = SDL_GetKeyboardState(NULL);
    SDL_GameController *c = (SDL_GameController *) sl_input_active_pad();

    if (!slot_ok(action, device, slot)) return;
    memset(&g_cap, 0, sizeof g_cap);
    g_cap.active = 1;
    g_cap.action = action; g_cap.device = device; g_cap.slot = slot;
    g_cap.stolen_from = -1;
    if (k != NULL) memcpy(g_cap.key_held, k, sizeof g_cap.key_held);
    /* Escape and the clear keys count as held too if they are down now, so
     * a held Escape cannot cancel on the first poll either. */
    g_cap.mouse_held = SDL_GetMouseState(NULL, NULL);
    g_cap.padb_held  = pad_button_mask(c);
    g_cap.pada_held  = pad_axis_mask(c);
    if (getenv("SL_INPUT_DEBUG") != NULL)
        fprintf(stderr, "sightline bindings: capture %s %s %d (held: mouse=%02x padb=%03x pada=%03x)\n",
                g_actions[action].token, device == SL_BIND_PAD ? "pad" : "kbm", slot + 1,
                g_cap.mouse_held, g_cap.padb_held, g_cap.pada_held);
}

int sl_bindings_capture_active(void)
{
    return g_cap.active;
}

int sl_bindings_capture_slot(int *action, int *device, int *slot)
{
    if (!g_cap.active) return 0;
    if (action) *action = g_cap.action;
    if (device) *device = g_cap.device;
    if (slot)   *slot   = g_cap.slot;
    return 1;
}

int sl_bindings_capture_stolen_from(void)
{
    return g_cap.stolen_from;
}

void sl_bindings_capture_cancel(void)
{
    if (!g_cap.active) return;
    g_cap.active = 0;
    g_cap.done_holding = 0;
    if (getenv("SL_INPUT_DEBUG") != NULL)
        fprintf(stderr, "sightline bindings: capture cancelled\n");
}

/* Is a source down right now, from the live devices? */
static int source_live(const sl_bind_source *s)
{
    const unsigned char *k;
    SDL_GameController *c;
    switch (s->kind) {
    case SL_SRC_KEY:
        k = SDL_GetKeyboardState(NULL);
        return k != NULL && k[s->code] != 0;
    case SL_SRC_MOUSE_BUTTON:
        return (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON(s->code)) != 0;
    case SL_SRC_PAD_BUTTON:
        c = (SDL_GameController *) sl_input_active_pad();
        return c != NULL && SDL_GameControllerGetButton(c, (SDL_GameControllerButton) s->code);
    case SL_SRC_PAD_AXIS:
        c = (SDL_GameController *) sl_input_active_pad();
        return c != NULL && SDL_GameControllerGetAxis(c, (SDL_GameControllerAxis) s->code) > SL_BIND_TRIG_ON;
    default:
        return 0;
    }
}

int sl_bindings_capture_blocking(void)
{
    if (g_cap.active) return 1;
    if (g_cap.done_holding) {
        if (source_live(&g_cap.done_src)) return 1;
        g_cap.done_holding = 0;
    }
    return 0;
}

static void capture_finish(const sl_bind_source *src)
{
    int stolen = -1;
    (void) sl_bindings_set(g_cap.action, g_cap.device, g_cap.slot, src, &stolen);
    g_cap.stolen_from = stolen;
    g_cap.active = 0;
    if (src->kind != SL_SRC_NONE && src->kind != SL_SRC_WHEEL) {
        g_cap.done_holding = 1;
        g_cap.done_src = *src;
    }
}

int sl_bindings_capture_poll(int wheel_up, int wheel_down)
{
    const unsigned char *k = SDL_GetKeyboardState(NULL);
    SDL_GameController *c = (SDL_GameController *) sl_input_active_pad();
    unsigned mouse = SDL_GetMouseState(NULL, NULL);
    unsigned padb = pad_button_mask(c), pada = pad_axis_mask(c);
    sl_bind_source src;
    int i;

    if (!g_cap.active) return 0;

    /* Release bookkeeping: anything held at begin is forgiven once it has
     * been seen up. */
    if (k != NULL)
        for (i = 0; i < SDL_NUM_SCANCODES; i++)
            if (g_cap.key_held[i] && !k[i]) g_cap.key_held[i] = 0;
    g_cap.mouse_held &= mouse;
    g_cap.padb_held  &= padb;
    g_cap.pada_held  &= pada;

    /* Backspace / Delete, clean: empty the slot. */
    if (k != NULL) {
        if ((k[SDL_SCANCODE_BACKSPACE] && !g_cap.key_held[SDL_SCANCODE_BACKSPACE])
            || (k[SDL_SCANCODE_DELETE] && !g_cap.key_held[SDL_SCANCODE_DELETE])) {
            src = g_none;
            capture_finish(&src);
            g_cap.done_holding = 1;
            g_cap.done_src.kind = SL_SRC_KEY;
            g_cap.done_src.code = k[SDL_SCANCODE_BACKSPACE] ? SDL_SCANCODE_BACKSPACE : SDL_SCANCODE_DELETE;
            g_cap.done_src.dir = 0;
            return 1;
        }
    }

    if (g_cap.device == SL_BIND_KBM) {
        if (k != NULL) {
            for (i = 0; i < N(g_keys); i++) {
                int sc = g_keys[i].code;
                if (k[sc] && !g_cap.key_held[sc]) {
                    src.kind = SL_SRC_KEY; src.code = (short) sc; src.dir = 0;
                    capture_finish(&src);
                    return 1;
                }
            }
        }
        for (i = 0; i < N(g_mouse); i++) {
            unsigned bit = SDL_BUTTON(g_mouse[i].code);
            if ((mouse & bit) && !(g_cap.mouse_held & bit)) {
                src.kind = SL_SRC_MOUSE_BUTTON; src.code = g_mouse[i].code; src.dir = 0;
                capture_finish(&src);
                return 1;
            }
        }
        if (wheel_up > 0 || wheel_down > 0) {
            src.kind = SL_SRC_WHEEL; src.code = 0;
            src.dir = (signed char) (wheel_up > 0 ? +1 : -1);
            capture_finish(&src);
            return 1;
        }
    } else {
        for (i = 0; i < N(g_pad); i++) {
            unsigned bit = 1u << i;
            if (g_pad[i].kind == SL_SRC_PAD_BUTTON) {
                if ((padb & bit) && !(g_cap.padb_held & bit)) {
                    src.kind = SL_SRC_PAD_BUTTON; src.code = g_pad[i].code; src.dir = 0;
                    capture_finish(&src);
                    return 1;
                }
            } else {
                if ((pada & bit) && !(g_cap.pada_held & bit)) {
                    src.kind = SL_SRC_PAD_AXIS; src.code = g_pad[i].code; src.dir = 1;
                    capture_finish(&src);
                    return 1;
                }
            }
        }
    }
    return 0;
}

#endif /* !__sgi */
