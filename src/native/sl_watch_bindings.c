/**
 * sl_watch_bindings.c - the watch's BINDINGS editor (#46): a NESTED CHILD
 * VIEW of the SIGHTLINE page (options.c), not a seventh ring page.
 *
 *     bindings                                           <- heading line
 *     device                keyboard     controller      <- row 0
 *     slot                  primary      secondary       <- row 1: the slot the rows show
 *     move forward          w
 *     ...                                                <- 14 action rows, ONE value
 *     zoom out              wheel down                      column, nine visible,
 *     reset defaults                                        the list scrolls
 *     back
 *     <message>
 *
 * WHY A CHILD AND NOT A PAGE. The player struct's two watch buffers are sized
 * by WATCH_NUMBER_SCREENS and the player is a fixed allocation (#44 recon:
 * bondview.h:1241 / :2182, player.c:127); the sixth page already rebuilds
 * the bar natively. A seventh would move nothing the original build sees
 * either, but it would put an EDITOR into the ring - L/R would step into and
 * out of it, and the bar would show a segment for something that is not a
 * page. So the SIGHTLINE page keeps a flag (open / closed) exactly as the
 * mission-status page keeps its abort sub-state (D_800409A4): while it is
 * open this file owns the page's navigation and draws in its place; the
 * bar keeps representing the ring and the ring's L/R do nothing until BACK.
 *
 * ONE RENDERING OF THE SHARED MODEL (src/platform/sl_bindings_editor.c); the
 * front end is the other. Only what is on THIS face is here: a stepped row
 * index with the watch's own up/down step (the U/D c-buttons, the d-pad, the
 * stick's y latch through sub_GAME_7F0A5088 / 50C4, wrapping), the A/Z
 * select LATCH (draw_watch_current_page toggles it for every page but the
 * inventory), and the toggle rows' idiom for the values: latch the row, then
 * LEFT = the first value, RIGHT = the second - on the DEVICE row the tab, on
 * SLOT which slot the action rows show and edit (PRIMARY / SECONDARY). (The
 * BUTTON MODE row left with the setting on 2026-09-20, #63: the pad's slots
 * are always live, and its presets are the BUTTON LAYOUT row of the CONTROLS
 * child.) On an ACTION row, on RESET DEFAULTS and on
 * BACK the latch itself is the press: an action's latch starts the capture
 * of its shown slot (PRESS A KEY / BUTTON in the value column). The pointer (#40 machinery,
 * sl_watch_pointer.c) hovers the row under it and clicks a value directly.
 *
 * Text is the page's (draw_options_labels, the toggle rows' colours and x
 * positions, lowercase in the text table's convention).
 */
#ifndef __sgi

#include <stdio.h>
#include <ultra64.h>
#include <bondgame.h>
#include <fr.h>
#include "player.h"
#include "options.h"
#include "joy.h"
#include "textrelated.h"
#include "../platform/sl_bindings_editor.h"

extern char *getenv(const char *);
extern u32  watch_screen_index;
extern s32  watch_item_is_actively_selected;
extern void reset_watch_item_is_actively_selected(void);
extern void disable_watch_stick_y_nav_ready(void);
extern s32  sub_GAME_7F0A5088(void);
extern s32  sub_GAME_7F0A50C4(void);
extern s32  sub_GAME_7F0A4FB0(void);
extern s32  sub_GAME_7F0A4FEC(void);
extern void watch_play_beep_sound(void);
extern void game_option_select_value(u32 *param_1, u32 param_2);

extern s32  g_WatchBackgroundGreen;

/* draw_options_labels' shape (options.c:2960: BankGothic at line height 10,
 * centred / left, the static-mode render mode, the optional background box,
 * plain or outlined), with ONE difference: the clip box handed to textRender
 * is the view (viGetX / viGetY), not the measured text box. Measured
 * 2026-09-18: with the measured box a glyph that reaches below line height
 * 10 is dropped whole - AIM's secondary "Q" drew as nothing on this face
 * while the same font draws it in the front end (frontPrintText passes the
 * view). The table's own "q watch" line also goes through textRender with
 * the view box (options.c:2198 onward). */
static Gfx *wb_label(Gfx *gdl, s32 x, s32 y, char *text, u32 colour, s32 outlined, u32 outlinecolour, s32 centre, s32 drawbg, u32 bgcolour)
{
    s32 textx, textwidth = 0, textheight = 0;

    textMeasure(&textheight, &textwidth, text, ptrFontBankGothicChars, ptrFontBankGothic, 10);
    textx = centre ? x - textwidth / 2 : x;

    if (g_WatchBackgroundGreen < 0xe0)
    {
        gDPSetRenderMode(gdl++, G_RM_AA_PCL_SURF, G_RM_AA_PCL_SURF2);
    }
    else
    {
        gDPSetRenderMode(gdl++, G_RM_AA_XLU_SURF, G_RM_AA_XLU_SURF2);
    }
    if (drawbg)
        gdl = microcode_constructor_related_to_menus(gdl, textx - 1, (y + outlined) + 1, textx + textwidth + 1, y + textheight + 1, bgcolour);
    gDPSetRenderMode(gdl++, G_RM_AA_XLU_SURF, G_RM_AA_XLU_SURF2);
    if (outlined)
        gdl = textRenderOutlined(gdl, &textx, &y, text, ptrFontBankGothicChars, ptrFontBankGothic, colour, outlinecolour, viGetX(), viGetY(), 0, 10);
    else
        gdl = textRender(gdl, &textx, &y, text, ptrFontBankGothicChars, ptrFontBankGothic, colour, viGetX(), viGetY(), 0, 10);
    return gdl;
}

#define DEV_KBM 0
#define DEV_PAD 1

/* Row kinds, in list order. */
enum { WB_DEVICE = 0, WB_SLOT, WB_ACTION, WB_RESET, WB_BACK };

static s32 s_open;
static s32 s_row;
static s32 s_top;             /* first visible row */
static s32 s_slot;            /* the slot the one value column shows: 0 PRIMARY, 1 SECONDARY */

/* The list for either device: DEVICE, SLOT, the actions, RESET, BACK. ONE
 * value column (the SLOT row picks which slot it shows):
 * measured 2026-09-18, BankGothic runs ~7.5 fb units a glyph on this face,
 * and a fifteen-glyph label plus two fourteen-glyph values do not fit the
 * round face's top rows (the right edge sits near fb 300 there); the
 * front end, on its wide paper, shows both columns. */
static s32 wb_first_action(void)
{
    return 2;
}

static s32 wb_count(void)
{
    return wb_first_action() + sl_bedit_action_count() + 2;
}

static s32 wb_kind(s32 row, s32 *action_i)
{
    s32 n = sl_bedit_action_count();
    s32 first = wb_first_action();
    if (action_i) *action_i = -1;
    if (row == 0) return WB_DEVICE;
    if (row == 1) return WB_SLOT;
    if (row - first < n) { if (action_i) *action_i = row - first; return WB_ACTION; }
    if (row - first == n) return WB_RESET;
    return WB_BACK;
}

static void wb_scroll_to_row(void)
{
    s32 count = wb_count();
    if (s_row < 0) s_row = 0;
    if (s_row >= count) s_row = count - 1;
    if (s_row < s_top) s_top = s_row;
    if (s_row >= s_top + SL_WATCH_BINDINGS_VISIBLE) s_top = s_row - (SL_WATCH_BINDINGS_VISIBLE - 1);
    if (s_top < 0) s_top = 0;
}

s32 sl_watch_bindings_is_open(void)
{
    return s_open;
}

void sl_watch_bindings_open(void)
{
    if (s_open) return;
    s_open = 1;
    s_row = 0;
    s_top = 0;
    s_slot = 0;
    sl_bedit_open(SL_BEDIT_SHELL_WATCH);
    reset_watch_item_is_actively_selected();
    if (getenv("SL_INPUT_DEBUG") != NULL)
        fprintf(stderr, "sightline bindings-watch: open (page=%u)\n", watch_screen_index);
}

void sl_watch_bindings_close(void)
{
    if (!s_open) return;
    s_open = 0;
    sl_bedit_close();
    reset_watch_item_is_actively_selected();
    if (getenv("SL_INPUT_DEBUG") != NULL)
        fprintf(stderr, "sightline bindings-watch: close -> sightline (page=%u)\n", watch_screen_index);
}

/* The row step (sub_GAME_7F0A5998's shape): up / down, the stick's y latch,
 * wrap, the select latch dropped on a row change. No page change - the
 * ring's L/R are the child's LEFT / RIGHT and act on the latched row only.
 * Runs from sl_watch_sightline_navigation (the game tick). */
void sl_watch_bindings_navigation(void)
{
    s32 count = wb_count();

    if (sl_bedit_take_back())
    {
        sl_watch_bindings_close();
        return;
    }
    if ((joyGetButtonsPressedThisFrame(PLAYER_1, U_CBUTTONS|U_JPAD)) || (sub_GAME_7F0A5088()))
    {
        s_row = s_row - 1;
        disable_watch_stick_y_nav_ready();
        reset_watch_item_is_actively_selected();
    }
    else if ((joyGetButtonsPressedThisFrame(PLAYER_1, D_CBUTTONS|D_JPAD)) || (sub_GAME_7F0A50C4()))
    {
        s_row = s_row + 1;
        disable_watch_stick_y_nav_ready();
        reset_watch_item_is_actively_selected();
    }
    if (s_row >= count) s_row = 0;
    if (s_row < 0) s_row = count - 1;
    wb_scroll_to_row();
}

/* A value chosen on a two-value row - by the latched LEFT / RIGHT or by a
 * click on the value. value 0 = first column, 1 = second. An ACTION row has
 * one value (the shown slot): choosing it starts the capture. */
static void wb_choose(s32 row, s32 value)
{
    s32 ai;
    u32 v;
    switch (wb_kind(row, &ai))
    {
    case WB_DEVICE:
        if (sl_bedit_device() != value)
        {
            v = (u32) sl_bedit_device();
            game_option_select_value(&v, (u32) value);   /* the sound + stick latch */
            sl_bedit_set_device(value);
            s_row = 0; s_top = 0;                         /* the list changed shape */
        }
        break;
    case WB_SLOT:
        if (s_slot != value)
        {
            v = (u32) s_slot;
            game_option_select_value(&v, (u32) value);
            s_slot = value;
        }
        break;
    case WB_ACTION:
        sl_bedit_activate_slot(ai, s_slot);
        reset_watch_item_is_actively_selected();
        break;
    default:
        break;
    }
    if (getenv("SL_INPUT_DEBUG") != NULL)
        fprintf(stderr, "sightline bindings-watch: row=%d value=%d -> device=%d capturing=%d\n",
                (int) row, (int) value, sl_bedit_device(), sl_bedit_capturing());
}

/* The latch pressed on a row that IS a press: reset, back, and an action
 * row (the capture of its shown slot - PRESS A KEY / BUTTON follows). */
static void wb_press(s32 row)
{
    switch (wb_kind(row, NULL))
    {
    case WB_RESET:
        reset_watch_item_is_actively_selected();
        sl_bedit_reset();
        break;
    case WB_BACK:
        sl_watch_bindings_close();
        break;
    case WB_ACTION:
        wb_choose(row, s_slot);
        break;
    default:
        break;
    }
}

/* Latched input, on the frame's buttons (the sl_sightline_row_input shape). */
static void wb_latched_input(s32 row)
{
    s32 k = wb_kind(row, NULL);
    if (!watch_item_is_actively_selected)
        return;
    if (k == WB_RESET || k == WB_BACK || k == WB_ACTION)
    {
        wb_press(row);
        return;
    }
    if (joyGetButtonsPressedThisFrame(PLAYER_1, L_CBUTTONS|L_TRIG|L_JPAD) || sub_GAME_7F0A4FB0())
        wb_choose(row, 0);
    else if (joyGetButtonsPressedThisFrame(PLAYER_1, R_CBUTTONS|R_TRIG|R_JPAD) || sub_GAME_7F0A4FEC())
        wb_choose(row, 1);
}

/* ---------------------------------------------------- the pointer's view -- */

/* For sl_watch_pointer.c: the visible rows and each one's texts. */
s32 sl_watch_bindings_visible(s32 *top, s32 *count)
{
    if (!s_open) return 0;
    if (top) *top = s_top;
    if (count) *count = wb_count();
    return 1;
}

/* The model's text in the table's LF convention. UPPERCASE as the model
 * gives it: the watch font draws the table's lowercase strings as capitals
 * anyway, and its lowercase 'q' has no glyph (measured 2026-09-18 - AIM's
 * secondary "Q" drew blank when lowercased, while the mission-status "Q
 * WATCH" draws), so nothing is lowercased here. */
static void lc(char *dst, s32 cap, const char *src)
{
    s32 i = 0;
    while (src[i] != '\0' && i < cap - 2)
    {
        dst[i] = src[i];
        i++;
    }
    dst[i++] = '\n';
    dst[i] = '\0';
}

/* Row texts in the table's LF convention; v0 / v1 empty when the row has
 * no value column there. An action row fills v0 only - the shown slot. */
void sl_watch_bindings_row_text(s32 row, char *label, char *v0, char *v1, s32 n)
{
    s32 ai;
    char tmp[48];
    label[0] = v0[0] = v1[0] = '\0';
    switch (wb_kind(row, &ai))
    {
    case WB_DEVICE:
        lc(label, n, "DEVICE"); lc(v0, n, "KEYBOARD"); lc(v1, n, "CONTROLLER");
        break;
    case WB_SLOT:
        lc(label, n, "SLOT"); lc(v0, n, "PRIMARY"); lc(v1, n, "SECONDARY");
        break;
    case WB_ACTION:
        lc(label, n, sl_bedit_action_label(ai));
        sl_bedit_slot_text(ai, s_slot, tmp, (s32) sizeof tmp); lc(v0, n, tmp);
        break;
    case WB_RESET:
        lc(label, n, "RESET DEFAULTS");
        break;
    default:
        lc(label, n, "BACK");
        break;
    }
}

void sl_watch_bindings_hover(s32 row)
{
    if (!s_open || row == s_row) return;
    s_row = row;
    disable_watch_stick_y_nav_ready();
    reset_watch_item_is_actively_selected();
    wb_scroll_to_row();
}

/* A click: on a label = what A does (highlight + latch; a press row acts -
 * an action row's press is the capture); on a value = choose it directly. */
void sl_watch_bindings_click(s32 row, s32 value)
{
    if (!s_open) return;
    if (row != s_row) sl_watch_bindings_hover(row);
    if (value < 0)
    {
        s32 k = wb_kind(row, NULL);
        if (k == WB_RESET || k == WB_BACK || k == WB_ACTION) { wb_press(row); return; }
        watch_play_beep_sound();
    }
    else
    {
        wb_choose(row, value);
    }
}

/* ------------------------------------------------------------- drawing -- */

/* A row, in the toggle rows' colours: the label highlighted / selected /
 * plain, the values left-aligned at the two columns, the current one (or
 * the waiting capture) lit. */
static Gfx *wb_draw_row(Gfx *gdl, s32 row, s32 y)
{
    char label[48], v0[48], v1[48];
    s32 ai, kind = wb_kind(row, &ai);
    s32 highlighted = row == s_row;
    s32 selected = highlighted && watch_item_is_actively_selected;
    u32 plain = 0x00FF00B0;
    u32 c0 = 0x00800080, c1 = 0x00800080;
    u32 active = selected ? 0xA0FFA0F0 : 0x00FF00B0;
    s32 x0 = kind == WB_ACTION ? SL_WATCH_BINDINGS_X_VALUE : SL_WATCH_BINDINGS_X_V0;
    s32 x1 = SL_WATCH_BINDINGS_X_V1;

    if (selected)
        wb_latched_input(row);
    if (!s_open)                    /* back / reset just closed or reshaped the list */
        return gdl;

    sl_watch_bindings_row_text(row, label, v0, v1, (s32) sizeof label);

    switch (kind)
    {
    case WB_DEVICE:
        if (sl_bedit_device() == DEV_PAD) c1 = active; else c0 = active;
        break;
    case WB_SLOT:
        if (s_slot) c1 = active; else c0 = active;
        break;
    case WB_ACTION:
        c0 = sl_bedit_slot_capturing(ai, s_slot) ? 0xA0FFA0F0 : plain;
        break;
    default:
        break;
    }

    if (selected)
        gdl = wb_label(gdl, XOFFSET_1, y, label, -1, 1, 0x7000A0, 0, 0, 0x3000B0);
    else if (highlighted)
        gdl = wb_label(gdl, XOFFSET_1, y, label, 0xA0FFA0F0, 0, -1, 0, 0, 0x3000B0);
    else
        gdl = wb_label(gdl, XOFFSET_1, y, label, 0xFF00B0, 0, -1, 0, 0, 0x3000B0);

    if (v0[0] != '\0')
        gdl = wb_label(gdl, x0, y, v0, c0, 0, -1, 0, 0, 0x3000B0);
    if (v1[0] != '\0')
        gdl = wb_label(gdl, x1, y, v1, c1, 0, -1, 0, 0, 0x3000B0);
    return gdl;
}

Gfx *sl_watch_bindings_draw(Gfx *gdl)
{
    static char heading[] = "bindings\n";
    char msg[64];
    s32 i, count;

    if (!s_open) return gdl;
    sl_bedit_tick();
    count = wb_count();
    wb_scroll_to_row();

    gdl = wb_label(gdl, XOFFSET_1, SL_SIGHTLINE_HEADING_Y, heading, 0xFF00B0, 0, -1, 0, 0, 0x3000B0);

    for (i = 0; i < SL_WATCH_BINDINGS_VISIBLE && s_top + i < count; i++)
    {
        gdl = wb_draw_row(gdl, s_top + i, SL_WATCH_BINDINGS_ROW_Y(i));
        if (!s_open) return gdl;
    }

    if (sl_bedit_message()[0] != '\0')
    {
        lc(msg, (s32) sizeof msg, sl_bedit_message());
        gdl = wb_label(gdl, XOFFSET_1, SL_WATCH_BINDINGS_MSG_Y, msg, 0xA0FFA0F0, 0, -1, 0, 0, 0x3000B0);
    }
    return gdl;
}

#endif /* !__sgi */
