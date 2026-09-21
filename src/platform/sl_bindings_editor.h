/**
 * sl_bindings_editor.h - the shared MODEL of the two BINDINGS editors (#46):
 * the front end's OPTIONS -> SETTINGS -> CONTROL -> BINDINGS screen
 * (src/native/sl_front_bindings.c) and the watch's SIGHTLINE -> BINDINGS
 * child view (src/native/sl_watch_bindings.c) are two RENDERINGS of this one
 * state. Everything that is not drawing or navigating lives here: which
 * device tab is up, the action list in editor order, what each slot reads,
 * starting a capture on a slot, RESET DEFAULTS, the short message after a
 * steal. Neither shell holds a binding of its own;
 * both read the registry (sl_bindings.h) through this, so what one shows is
 * what the other shows and there is nothing to synchronise.
 *
 * Host-clean (plain ints and char *): src/native includes it against the
 * N64 tree, src/platform against the host headers.
 */
#ifndef SL_BINDINGS_EDITOR_H
#define SL_BINDINGS_EDITOR_H

#ifdef __sgi
#error "sl_bindings_editor.h is native-only"
#endif

#define SL_BEDIT_SHELL_FRONT 0
#define SL_BEDIT_SHELL_WATCH 1

/* Open for one shell (resets the tab to KEYBOARD/MOUSE, drops any capture);
 * close (drops any capture). Only one shell is ever open. */
void sl_bedit_open(int shell);
void sl_bedit_close(void);
int  sl_bedit_is_open(void);
int  sl_bedit_shell(void);

/* The platform layer's Escape in the watch shell: asks the shell to go BACK
 * one level. The shell takes the request on its next frame. */
void sl_bedit_request_back(void);
int  sl_bedit_take_back(void);

/* The device tab: SL_BIND_KBM / SL_BIND_PAD (sl_bindings.h). */
int  sl_bedit_device(void);
void sl_bedit_set_device(int device);

/* (The controller BUTTON MODE and the PAD tab's dimming were removed on
 * 2026-09-20 with the pad_button_mode setting, #63: the PAD slots are
 * always live, and the BUTTON LAYOUT presets live in sl_bindings.h.) */

/* The action list in EDITOR order (movement, fire / aim, then the rest). */
int         sl_bedit_action_count(void);
int         sl_bedit_action_at(int i);           /* enum sl_action */
const char *sl_bedit_action_label(int i);        /* "MOVE FORWARD" */

/* What a slot of row i on the current device reads: the source's display
 * name, "---" when empty, or the capture prompt while that slot waits. */
void sl_bedit_slot_text(int i, int slot, char *buf, int n);
int  sl_bedit_slot_capturing(int i, int slot);
int  sl_bedit_capturing(void);

/* Start a capture on row i's slot (the shells' confirm / click on a value).
 * A second activation on the waiting slot cancels. */
void sl_bedit_activate_slot(int i, int slot);

/* RESET DEFAULTS: bindings only. */
void sl_bedit_reset(void);

/* The message line ("TAKEN FROM RELOAD", "DEFAULTS RESTORED"), "" when
 * none; tick once per frame from the open shell to age it and to notice a
 * capture that completed since the last frame. */
const char *sl_bedit_message(void);
void        sl_bedit_tick(void);

/* The capture prompt the shells print in a waiting slot. */
#define SL_BEDIT_PROMPT_KBM "PRESS A KEY"
#define SL_BEDIT_PROMPT_PAD "PRESS A BUTTON"

#endif /* SL_BINDINGS_EDITOR_H */
