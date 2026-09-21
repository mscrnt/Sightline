/**
 * sl_bindings_editor.c - the shared editor model (#46). See the header.
 *
 * WHAT IS HERE AND WHAT IS NOT. Here: the device tab, the editor order of
 * the actions, slot text, starting / cancelling a capture through the
 * registry's capture API, reset, the message. (The BUTTON MODE setting and
 * the PAD tab's dimming left on 2026-09-20 with the setting itself, #63:
 * the PAD slots are always live.) Not
 * here: any row index, cursor, scroll window, hit band or colour - those are
 * the shells', because the front end is a pointer menu with hit bands on a
 * paper page and the watch is a stepped list with a latch on a round face,
 * and they navigate differently on purpose (each the way its own menu
 * system already does).
 */
#ifndef __sgi
#include "sl_bindings_editor.h"
#include "sl_bindings.h"
#include "sl_action.h"
#include "sl_settings.h"
#include <stdio.h>
#include <string.h>

/* Editor order: what a player expects to read top to bottom. */
static const unsigned char g_order[] = {
    SL_ACT_MOVE_FORWARD, SL_ACT_MOVE_BACK, SL_ACT_STRAFE_LEFT, SL_ACT_STRAFE_RIGHT,
    SL_ACT_FIRE, SL_ACT_AIM,
    SL_ACT_INTERACT, SL_ACT_RELOAD, SL_ACT_CROUCH, SL_ACT_SPRINT,
    SL_ACT_WEAPON_PREVIOUS, SL_ACT_WEAPON_NEXT,
    SL_ACT_ZOOM_IN, SL_ACT_ZOOM_OUT
};
#define N_ORDER ((int) (sizeof g_order / sizeof g_order[0]))

#define SL_BEDIT_MESSAGE_TICKS 120

static int  g_open;
static int  g_shell;
static int  g_device;
static int  g_back_requested;
static int  g_was_capturing;
static char g_message[48];
static int  g_message_ticks;

static void set_message(const char *m)
{
    sl_bind_copy(g_message, (int) sizeof g_message, m);
    g_message_ticks = SL_BEDIT_MESSAGE_TICKS;
}

void sl_bedit_open(int shell)
{
    sl_bindings_capture_cancel();
    g_open = 1;
    g_shell = shell;
    g_device = SL_BIND_KBM;
    g_back_requested = 0;
    g_was_capturing = 0;
    g_message[0] = '\0';
    g_message_ticks = 0;
}

void sl_bedit_close(void)
{
    sl_bindings_capture_cancel();
    g_open = 0;
    g_back_requested = 0;
}

int sl_bedit_is_open(void)
{
    return g_open;
}

int sl_bedit_shell(void)
{
    return g_shell;
}

void sl_bedit_request_back(void)
{
    if (g_open) g_back_requested = 1;
}

int sl_bedit_take_back(void)
{
    int r = g_back_requested;
    g_back_requested = 0;
    return r;
}

int sl_bedit_device(void)
{
    return g_device;
}

void sl_bedit_set_device(int device)
{
    if (device < 0 || device >= SL_BIND_DEVICES || device == g_device) return;
    sl_bindings_capture_cancel();
    g_device = device;
}

int sl_bedit_action_count(void)
{
    return N_ORDER;
}

int sl_bedit_action_at(int i)
{
    return (i >= 0 && i < N_ORDER) ? g_order[i] : -1;
}

const char *sl_bedit_action_label(int i)
{
    return sl_bindings_action_label(sl_bedit_action_at(i));
}

int sl_bedit_slot_capturing(int i, int slot)
{
    int a, d, s;
    if (!sl_bindings_capture_slot(&a, &d, &s)) return 0;
    return a == sl_bedit_action_at(i) && d == g_device && s == slot;
}

int sl_bedit_capturing(void)
{
    return sl_bindings_capture_active();
}

void sl_bedit_slot_text(int i, int slot, char *buf, int n)
{
    if (buf == NULL || n <= 0) return;
    if (sl_bedit_slot_capturing(i, slot)) {
        sl_bind_copy(buf, n, g_device == SL_BIND_PAD ? SL_BEDIT_PROMPT_PAD : SL_BEDIT_PROMPT_KBM);
        return;
    }
    sl_bindings_source_name(sl_bindings_get(sl_bedit_action_at(i), g_device, slot), buf, n);
}

void sl_bedit_activate_slot(int i, int slot)
{
    int a = sl_bedit_action_at(i);
    if (a < 0 || slot < 0 || slot >= SL_BIND_SLOTS) return;
    if (sl_bedit_slot_capturing(i, slot)) {
        sl_bindings_capture_cancel();
        return;
    }
    sl_bindings_capture_begin(a, g_device, slot);
    g_was_capturing = 1;
}

void sl_bedit_reset(void)
{
    sl_bindings_capture_cancel();
    sl_bindings_reset_defaults();
    set_message("DEFAULTS RESTORED");
}

const char *sl_bedit_message(void)
{
    return g_message_ticks > 0 ? g_message : "";
}

void sl_bedit_tick(void)
{
    if (g_message_ticks > 0) g_message_ticks--;
    /* A capture that ended since the last frame: say what it took. */
    if (g_was_capturing && !sl_bindings_capture_active()) {
        int from = sl_bindings_capture_stolen_from();
        g_was_capturing = 0;
        if (from >= 0) {
            char m[48];
            snprintf(m, sizeof m, "TAKEN FROM %s", sl_bindings_action_label(from));
            set_message(m);
        }
    }
}
#endif /* !__sgi */
