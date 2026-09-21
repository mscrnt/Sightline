/**
 * sl_bindings.h - the persistent BINDING REGISTRY (#46): which physical
 * controls raise each gameplay action, editable by the player, persisted in
 * config.ini, read by the input path and by BOTH editors.
 *
 * WHAT THIS IS. One table, four slots per action:
 *
 *     device KBM   slot PRIMARY, slot SECONDARY    keys, mouse buttons, wheel
 *     device PAD   slot PRIMARY, slot SECONDARY    controller buttons, trigger
 *                                                  halves (digital)
 *
 * Every slot holds one source or nothing. The compiled default table lives
 * HERE and only here (sl_bindings.c g_defaults): the input path evaluates the
 * live table (src/platform/sl_action.c reads it row by row), the front-end
 * editor and the watch editor read and write it through the same three calls
 * (get / set / reset), and the persisted form is derived from it - so there is
 * no second copy of any default in input code, in a UI or in the parser, and
 * the two menus cannot disagree because there is nothing between them.
 *
 * WHAT IT IS NOT. Not the action semantics (sl_action.h names the actions and
 * how a held / pressed state is derived), not the store (sl_settings.c owns
 * the file), not a UI (sl_bindings_editor.c is the shared editor MODEL, the
 * shells render it). Analog axes - mouse X/Y, the sticks - are not bindings
 * and never appear here; they stay on their own paths.
 *
 * PERSISTED FORM. One line per NON-DEFAULT slot in config.ini, through the
 * store's bounded extension for "bind." keys (sl_settings_ext_*):
 *
 *     bind.<action>.<device>.<slot>=<token>
 *     bind.move_forward.kbm.1=key:UP
 *     bind.fire.kbm.2=none
 *
 * <action> is the lowercase action token (sl_bindings_action_token), <device>
 * is kbm or pad, <slot> is 1 or 2, <token> is a stable human-readable source
 * token (never a raw SDL number): key:<NAME> from the key table below,
 * mouse:LEFT|RIGHT|MIDDLE|X1|X2, wheel:UP|DOWN, pad:A|B|X|Y|LB|RB|LT|RT|LS|RS
 * |DPAD_UP|DPAD_DOWN|DPAD_LEFT|DPAD_RIGHT (the four since round 6), or none
 * for an emptied slot. A slot that equals its default has no line
 * (missing override = compiled default); a malformed token leaves THAT slot
 * at its default; keys naming an action or source this build does not know
 * are ignored and preserved. Old configs without any bind. line load
 * unchanged. No version bump.
 *
 * CONTEXT, and the conflict policy. A source may sit in only one slot of one
 * action within a CONTEXT. The context is registry metadata, not a UI rule
 * (sl_bindings_source_ctx, ONE rule since round 6 of #63 / #64): a row of a
 * CONTEXTUAL source - the wheel, and the two bumpers LB / RB - on a
 * WEAPON-CYCLE action is live in ordinary play and on a ZOOM action while
 * aiming with an adjustable scope (sl_action.h sl_wheel_ctx), so wheel up
 * may be PREVIOUS WEAPON and ZOOM IN at once (the accepted #38 default) and
 * RB may be NEXT WEAPON and ZOOM IN at once (the round-6 default: the
 * bumpers cycle weapons on foot and zoom in the scope). Every other row - a
 * non-contextual source on anything, or a contextual source on an action
 * outside those two pairs - is a level with no context and conflicts with
 * any use of the same source anywhere. Setting a slot to a source another
 * slot holds in the same context STEALS it: the other slot is emptied and
 * the caller is told which action lost it.
 *
 * Host-clean header (plain ints and char *), so src/platform, src/native and
 * the tests all include it. Native only, like sl_action.h.
 */
#ifndef SL_BINDINGS_H
#define SL_BINDINGS_H

#ifdef __sgi
#error "sl_bindings.h is native-only"
#endif

/* Devices and slots. */
enum sl_bind_device { SL_BIND_KBM = 0, SL_BIND_PAD = 1, SL_BIND_DEVICES = 2 };
#define SL_BIND_SLOTS 2

/* One source. kind is enum sl_src_kind (sl_action.h); code / dir per kind:
 *   SL_SRC_KEY           code = SDL_Scancode
 *   SL_SRC_MOUSE_BUTTON  code = SDL button index (1 left .. 5 x2)
 *   SL_SRC_WHEEL         dir = +1 up, -1 down
 *   SL_SRC_PAD_BUTTON    code = SDL_GameControllerButton
 *   SL_SRC_PAD_AXIS      code = SDL_GameControllerAxis (a trigger), dir = +1
 *   SL_SRC_NONE          an empty slot */
typedef struct {
    unsigned char kind;
    short         code;
    signed char   dir;
} sl_bind_source;

/* ---------------------------------------------------------------- table -- */

/* Load: the compiled defaults, then every bind. override the store holds.
 * Called by the shim once the store is initialised (before the first poll);
 * the first evaluation calls it too if nothing else has, so a build with no
 * store (tests, headless) runs the defaults. Idempotent. */
void sl_bindings_init(void);

/* Drop the live table and load again (the shim, right after the store
 * loads, so the store's overrides win over any earlier default-only init;
 * the tests, between fixtures). Cancels any capture. */
void sl_bindings_reload(void);

/* The source in a slot (never NULL; kind SL_SRC_NONE for an empty slot). */
const sl_bind_source *sl_bindings_get(int action, int device, int slot);

/* Set a slot. Applies the conflict policy (the stolen-from action, if any,
 * is written to *stolen_from, else -1), persists every slot that changed,
 * returns 1 if anything changed. A source of the wrong device class for the
 * slot (a key into a PAD slot) is refused. NULL / SL_SRC_NONE empties. */
int  sl_bindings_set(int action, int device, int slot, const sl_bind_source *src,
                     int *stolen_from);

/* Every slot back to the compiled default; every bind. line removed from the
 * store in one write. Bindings only - no other setting is touched. */
void sl_bindings_reset_defaults(void);

/* Is the slot at its compiled default? */
int  sl_bindings_is_default(int action, int device, int slot);

/* The compiled default for a slot, for the editors' "(default)" hint. */
const sl_bind_source *sl_bindings_default(int action, int device, int slot);

/* THE ACTION'S BINDING GENERATION (#56): a counter that advances every time
 * any slot of `action` is written - an editor's capture or clear, a steal
 * from it, a preset applied, RESET DEFAULTS, a (re)load. The action layer's
 * HOLD / TOGGLE latch (sl_action.c) compares it poll to poll and drops the
 * latch when the action's sources change, so a latch never outlives the
 * control that set it. Monotonic within a process; 0 out of range. */
unsigned sl_bindings_generation(int action);

/* Which sl_wheel_ctx a contextual source on this action is live in (the
 * action's own context: ZOOM IN / OUT scoped, everything else play). */
int  sl_bindings_wheel_ctx(int action);

/* THE ROW'S CONTEXT (round 6): SL_WHEEL_CTX_PLAY or _SCOPED for a row of a
 * contextual source (the wheel, LB, RB) on a weapon-cycle or zoom action;
 * SL_CTX_NONE for every other row - a level with no context. The evaluator
 * and the conflict policy both read this and nothing else. */
#define SL_CTX_NONE (-1)
int  sl_bindings_source_ctx(const sl_bind_source *src, int action);

/* The collective brief of a complementary pair ("WEAPONS" for PREVIOUS /
 * NEXT WEAPON, "ZOOM" for ZOOM IN / OUT) when the two actions are the two
 * halves of one, else NULL - the watch page prints it for a control that
 * holds both halves (the d-pad). */
const char *sl_bindings_pair_brief(int action_a, int action_b);

/* ------------------------------------------------------ button layouts -- */

/* The named PAD presets (#63): SL_BUTTON_LAYOUT_* of sl_settings.h. The
 * name of a preset or of CUSTOM ("?" out of range); the layout the store
 * records (CUSTOM when unrecognised); a preset's source for one PAD slot
 * (SL_SRC_NONE for CUSTOM / out of range); and APPLY - every action's two
 * PAD slots written from the preset in one store write, the id recorded.
 * Returns 1 if anything changed. Editing a PAD slot through
 * sl_bindings_set records CUSTOM (or the preset the table then equals);
 * sl_bindings_reset_defaults records DEFAULT. */
const char           *sl_bindings_layout_name(int layout);
int                   sl_bindings_layout_get(void);
const sl_bind_source *sl_bindings_layout_source(int layout, int action, int slot);
int                   sl_bindings_layout_apply(int layout);
/* Is this preset offered to the player? 0 for CUSTOM, for an id out of
 * range and for a RETIRED preset - one whose id stays occupied (the setting
 * is persisted and its ids are append-only) but which is never offered,
 * never applied and never matched. */
int                   sl_bindings_layout_selectable(int layout);
/* The next selectable preset from `cur` in direction `dir` (+1 / -1),
 * wrapping and skipping retired ones; a step off CUSTOM lands on the first
 * going up, the last going down. The editors' one stepper. */
int                   sl_bindings_layout_step(int cur, int dir);

/* THE LABEL RESOLVER (#63, the watch's controller page): every action that
 * holds `src` in either of its `device` slots, in enum sl_action order, at
 * most `max` written to `actions`; returns the count (which may exceed
 * `max`). 0 for an empty source or one bound nowhere. */
int  sl_bindings_actions_for_source(const sl_bind_source *src, int device,
                                    int *actions, int max);

/* ------------------------------------------------------- names, tokens -- */

/* "move_forward" - the persisted token; NULL out of range. */
const char *sl_bindings_action_token(int action);
/* "MOVE FORWARD" - the editors' label; "?" out of range. */
const char *sl_bindings_action_label(int action);
/* "PREV WPN" - the brief form (at most eight glyphs) the watch's controller
 * page prints beside a button name (#63); the label itself where it is
 * already that short. "?" out of range. */
const char *sl_bindings_action_brief(int action);
/* The action with this token, or -1. */
int  sl_bindings_action_from_token(const char *token);

/* "key:LSHIFT" / "mouse:LEFT" / "wheel:UP" / "pad:RT" / "none" into buf;
 * returns buf. A source outside the tables formats as "none". */
const char *sl_bindings_source_token(const sl_bind_source *src, char *buf, int n);
/* The reverse; 1 on success (including "none" -> SL_SRC_NONE), 0 malformed. */
int  sl_bindings_source_parse(const char *token, sl_bind_source *out);
/* "LEFT SHIFT" / "MOUSE 1" / "WHEEL UP" / "PAD RT" / "---" into buf. A pad
 * source is labelled for the ATTACHED family (#63): "RT" on an Xbox-family
 * pad, "R2" on a PlayStation-family one, "PAD RT" otherwise. The token
 * (above) never changes with the family. */
const char *sl_bindings_source_name(const sl_bind_source *src, char *buf, int n);
/* The same, for an explicit SL_PAD_FAMILY_* - what the editors and the tests
 * call when the family is not the attached pad's. */
const char *sl_bindings_source_label(const sl_bind_source *src, int family,
                                     char *buf, int n);
/* Which device class a source belongs to (SL_BIND_KBM / SL_BIND_PAD), or -1
 * for SL_SRC_NONE. */
int  sl_bindings_source_device(const sl_bind_source *src);
/* Is this a source the editors accept (in the tables, not a reserved key
 * such as Escape / Tab / Enter / the mark keys, not a stick axis)? */
int  sl_bindings_source_capturable(const sl_bind_source *src);
/* Two sources the same? */
int  sl_bindings_source_equal(const sl_bind_source *a, const sl_bind_source *b);

/* -------------------------------------------------------------- capture -- */

/* Start listening for the next valid source for one slot. Everything held
 * at this moment (keys, mouse buttons, pad buttons and triggers) is ignored
 * until released - which is what keeps the click or the confirm that opened
 * the capture from becoming the binding. No timeout. */
void sl_bindings_capture_begin(int action, int device, int slot);

/* Is a capture waiting? */
int  sl_bindings_capture_active(void);

/* Is a capture waiting, OR has one just completed with its source still
 * held? The input path neutralises menu input for the whole of that window,
 * so the captured key cannot also move a cursor or act as a button on the
 * tick it landed. */
int  sl_bindings_capture_blocking(void);

/* The slot being captured; returns 0 when none. */
int  sl_bindings_capture_slot(int *action, int *device, int *slot);

/* Escape: nothing changes. */
void sl_bindings_capture_cancel(void);

/* One poll of the devices while a capture waits, called by sl_input.c with
 * this poll's wheel notches (the keyboard, the mouse buttons and the active
 * pad are read directly). Returns 1 when the capture completed this poll -
 * bound, cleared (Backspace / Delete) - and 0 otherwise. */
int  sl_bindings_capture_poll(int wheel_up, int wheel_down);

/* The last completed capture's outcome, for the editors' message: the action
 * a source was stolen from (-1 none). Cleared by the next begin. */
int  sl_bindings_capture_stolen_from(void);

/* Diagnostics: print the whole table to stderr. */
void sl_bindings_dump(void);

/* A bounded string copy writing at most n bytes (always terminated). Used
 * instead of strncpy throughout the registry and the editor model: see the
 * note at its definition in sl_bindings.c. */
void sl_bind_copy(char *dst, int n, const char *src);

#endif /* SL_BINDINGS_H */
