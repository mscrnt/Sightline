/**
 * sl_front_bindings.c - the front end's BINDINGS editor (#46):
 * OPTIONS -> SETTINGS -> CONTROL -> BINDINGS -> MENU_SL_BINDINGS.
 *
 *     KEYBOARD/MOUSE  CONTROLLER                     <- device tabs
 *                     PRIMARY      SECONDARY         <- header
 *     MOVE FORWARD    W            ---
 *     MOVE BACK       S            ---
 *     ...             ...          ...               <- 14 action rows
 *     ZOOM OUT        WHEEL DOWN   ---
 *     RESET DEFAULTS               BACK              <- footer
 *
 * ONE RENDERING OF THE SHARED MODEL (src/platform/sl_bindings_editor.c). The
 * device tab, the list, what each slot reads, starting a capture, reset and
 * the message all come from it; this file only lays them out the
 * way the Settings page lays out its rows (sl_front_options.c: hit bands on
 * cursor_v_pos, value columns on cursor_h_pos, the highlight box on the
 * element under the cursor, confirm on A / Z / START, B and the PREVIOUS
 * tab back, frontUpdateControlStickPosition so the stick, the keyboard and
 * the pointer all move the one front-end cursor, frontDrawCursor). A mouse
 * click is one N64 A edge on a screen sl_game_pointer_menu_active lists; a
 * click on a VALUE captures that slot (the #40 rule: act on what the cursor
 * is over).
 *
 * FOURTEEN ROWS AT A 16-UNIT PITCH, no scrolling: the paper runs from the
 * strip at y 0x1E to the footer at 0x122, and the last row's box ends at
 * 0x11C - inside the page (the PREVIOUS tab sits right of x 390, clear of
 * every column). The cheat menu's 0x14 pitch would not fit 14 rows plus the
 * header and footer.
 *
 * CAPTURE. Confirming a slot (or clicking it) asks the model to capture; the
 * platform layer then owns the devices - the next key / button lands in the
 * slot, Escape cancels, Backspace / Delete empties, the click or key that
 * started it is ignored until released - and neutralises the menu's input
 * until the captured source is released (sl_input.c sl_bindings_capture_
 * blocking), so this function sees no A / B / stick from the press that
 * became the binding. It draws the prompt in the waiting slot and nothing
 * else changes.
 *
 * (The CONTROLLER tab's BUTTON MODE header and its dimmed rows left with the
 * setting on 2026-09-20, #63: the pad's slots are always live; the presets
 * that seed them are the PAD tab's BUTTON LAYOUT row.)
 *
 * Everything is !__sgi; front.c carries the three dispatch cases, guarded.
 */
#ifndef __sgi

#include <stdio.h>
#include <ultra64.h>
#include <bondgame.h>
#include <boss.h>
#include <fr.h>
#include <music.h>
#include <snd.h>
#include "joy.h"
#include "front.h"
#include "textrelated.h"
#include "../platform/sl_bindings_editor.h"

extern Gfx  *frontPrintText(Gfx *gdl, s32 *x, s32 *y, s8 *text, s32 second_font_table, s32 first_font_table, s32 arg6, s32 view_x, s32 view_y, s32 arg9, s32 arga);
extern Gfx  *frontSetupMenuBackground(Gfx *DL);
extern Gfx  *frontAddPreviousTabText(Gfx *DL);
extern Gfx  *frontDrawCursor(Gfx *DL);
extern u32   frontCheckCursorOnPreviousTab(void);
extern void  frontUpdateControlStickPosition(void);
extern void  disable_all_switches(Model *arg0);
extern void  set_item_visibility_in_objinstance(Model *objinstance, s32 item, s32 mode);
extern void  load_walletbond(void);
extern s32   tab_prev_selected;
extern s32   tab_prev_highlight;
extern void  sl_front_witness(s32 menu, s32 row, s32 col);
extern void  sl_front_settings_return_from_bindings(void);
extern char *getenv(const char *);

/* sl_bindings.h's device ids, restated (that header is host-clean too, but
 * this file needs only the two numbers). */
#define DEV_KBM 0
#define DEV_PAD 1

#define FONT_CHARS ((s32) ptrFontZurichBoldChars)
#define FONT       ((s32) ptrFontZurichBold)
/* The TABLE is set in the front end's smaller face, BankGothic - the one the
 * difficulty page and the multiplayer stage list use (front.c:3888, :6540)
 * - because ZurichBold at ~9.5 units a glyph cannot fit a label, two slot
 * values up to fourteen characters (PRESS A BUTTON, RIGHT BRACKET) and the
 * PREVIOUS tab across 390 units (measured 2026-09-18: WHEEL DOWN ran into
 * the SECONDARY column and PREVIOUS WEAPON into PRIMARY). The tabs and the
 * footer stay ZurichBold like every other front-end control. */
#define TFONT_CHARS ((s32) ptrFontBankGothicChars)
#define TFONT       ((s32) ptrFontBankGothic)
#define CONFIRM    (START_BUTTON | Z_TRIG | A_BUTTON)
#define SHADE_DIM  0x70
#define SHADE_ON   0xA00000FF

/* Layout. Tabs and footer in the Settings page's label column; the table's
 * value columns further right than the Settings page's, for the values. */
#define BND_X_LABEL   0x37
#define BND_X_TAB1    0xC8
#define BND_X_VAL0    0xC0
#define BND_X_VAL1    0x120
#define BND_X_BACK    0x10E
#define BND_TAB_Y     0x1E
#define BND_HDR_Y     0x2E
#define BND_ROW_Y(i)  (0x3E + (i) * 0x10)
#define BND_FOOT_Y    0x122
#define BND_MSG_Y     0x134
#define BND_TOP(y)    ((f32) ((y) - 9))
#define BND_COL_TOP(x) ((f32) ((x) - 6))

/* The highlighted element. */
#define BND_ROW_TABS   (-2)
#define BND_ROW_HDR    (-3)
#define BND_ROW_FOOT   (-4)

static s32 s_row;     /* BND_ROW_* or an action index */
static s32 s_col;     /* -1 label, 0 / 1 value (tabs: the tab; footer: 0 reset, 1 back) */

void sl_init_menu_bindings(void)
{
    tab_start_selected = FALSE;
    tab_next_selected = FALSE;
    tab_prev_selected = FALSE;
    tab_prev_highlight = FALSE;
    tab_next_highlight = FALSE;
    tab_start_highlight = FALSE;
    sl_bedit_open(SL_BEDIT_SHELL_FRONT);
    s_row = 0;
    s_col = -1;
    load_walletbond();
    cursor_h_pos = 126.0f;
    cursor_v_pos = (f32) (BND_ROW_Y(0) + 6);
}

/* One press on what the cursor is over. */
static void bnd_activate(s32 row, s32 col)
{
    if (row == BND_ROW_TABS)
    {
        sl_bedit_set_device(col == 1 ? DEV_PAD : DEV_KBM);
    }
    else if (row == BND_ROW_FOOT)
    {
        if (col == 1)
            tab_prev_selected = TRUE;
        else
            sl_bedit_reset();
    }
    else if (row >= 0 && row < sl_bedit_action_count())
    {
        sl_bedit_activate_slot(row, col < 0 ? 0 : col);
    }
    if (getenv("SL_INPUT_DEBUG") != NULL)
        fprintf(stderr, "sightline bindings-front: row=%d col=%d -> device=%d capturing=%d\n",
                (int) row, (int) col, sl_bedit_device(), sl_bedit_capturing());
}

void sl_interface_menu_bindings(void)
{
    s32 i, n = sl_bedit_action_count();

    viSetFovY(FOV_Y_F);
    viSetAspect(ASPECT_RATIO_SD);
    viSetZRange(100.0f, 10000.0f);
    viSetUseZBuf(FALSE);

    sl_bedit_tick();

    /* The highlight follows the cursor while A/Z are up (the cheat menu's
     * rule), so a press acts on what was under the cursor when it arrived. */
    if (joyGetButtons(PLAYER_1, A_BUTTON | Z_TRIG) == 0)
    {
        tab_prev_highlight = FALSE;
        s_row = BND_ROW_TABS;
        s_col = 0;
        if (frontCheckCursorOnPreviousTab())
        {
            tab_prev_highlight = TRUE;
        }
        else if (cursor_v_pos >= BND_TOP(BND_FOOT_Y))
        {
            s_row = BND_ROW_FOOT;
            s_col = cursor_h_pos >= BND_COL_TOP(BND_X_BACK) ? 1 : 0;
        }
        else if (cursor_v_pos >= BND_TOP(BND_ROW_Y(0)))
        {
            i = (s32) ((cursor_v_pos - BND_TOP(BND_ROW_Y(0))) / 0x10);
            if (i >= n) i = n - 1;
            s_row = i;
            s_col = -1;
            if (cursor_h_pos >= BND_COL_TOP(BND_X_VAL1))      s_col = 1;
            else if (cursor_h_pos >= BND_COL_TOP(BND_X_VAL0)) s_col = 0;
        }
        else if (cursor_v_pos >= BND_TOP(BND_HDR_Y))
        {
            s_row = BND_ROW_HDR;
            s_col = -1;
            if (cursor_h_pos >= BND_COL_TOP(BND_X_VAL1))      s_col = 1;
            else if (cursor_h_pos >= BND_COL_TOP(BND_X_VAL0)) s_col = 0;
        }
        else
        {
            s_row = BND_ROW_TABS;
            s_col = cursor_h_pos >= BND_COL_TOP(BND_X_TAB1) ? 1 : 0;
        }
    }

    if (joyGetButtonsPressedThisFrame(PLAYER_1, CONFIRM))
    {
        if (tab_prev_highlight)
            tab_prev_selected = TRUE;
        else
            bnd_activate(s_row, s_col);
        sndPlaySfx(g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, 0);
    }
    else if (joyGetButtonsPressedThisFrame(PLAYER_1, B_BUTTON))
    {
        tab_prev_selected = TRUE;
        sndPlaySfx(g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, 0);
    }

    disable_all_switches(walletinst[0]);
    set_item_visibility_in_objinstance(walletinst[0], SW_TABS, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_BLANK, 1);
    sl_front_witness(MENU_SL_BINDINGS, tab_prev_highlight ? -1 : s_row, s_col);
    frontUpdateControlStickPosition();

    if (tab_prev_selected)
    {
        sl_bedit_close();
        sl_front_settings_return_from_bindings();
    }
}

/* A front-end control (tabs, footer): ZurichBold, the Settings page's box. */
static Gfx *bnd_text(Gfx *DL, s32 x, s32 y, s8 *text, s32 colour, s32 boxed)
{
    s32 w, h, px = x, py = y;

    if (boxed)
    {
        textMeasure(&h, &w, (char *) text, ptrFontZurichBoldChars, ptrFontZurichBold, 0);
        DL = microcode_constructor_related_to_menus(DL, x - 2, y - 1, x + w + 5, y + 0xE, 0x32);
    }
    return frontPrintText(DL, &px, &py, text, FONT_CHARS, FONT, colour, viGetX(), viGetY(), 0, 0);
}

/* A table cell (header, rows, message): BankGothic, the same box. */
static Gfx *bnd_cell(Gfx *DL, s32 x, s32 y, s8 *text, s32 colour, s32 boxed)
{
    s32 w, h, px = x, py = y;

    if (boxed)
    {
        textMeasure(&h, &w, (char *) text, ptrFontBankGothicChars, ptrFontBankGothic, 0);
        DL = microcode_constructor_related_to_menus(DL, x - 2, y - 1, x + w + 5, y + 0xE, 0x32);
    }
    return frontPrintText(DL, &px, &py, text, TFONT_CHARS, TFONT, colour, viGetX(), viGetY(), 0, 0);
}

/* A native label with the table's LF convention, from a plain string. */
static s8 *bnd_lf(char *dst, s32 cap, const char *src)
{
    s32 i = 0;
    while (src[i] != '\0' && i < cap - 2) { dst[i] = src[i]; i++; }
    dst[i++] = '\n';
    dst[i] = '\0';
    return (s8 *) dst;
}

Gfx *sl_constructor_menu_bindings(Gfx *DL)
{
    static char l_kbm[]   = "KEYBOARD/MOUSE\n";
    static char l_pad[]   = "CONTROLLER\n";
    static char l_prim[]  = "PRIMARY\n";
    static char l_sec[]   = "SECONDARY\n";
    static char l_reset[] = "RESET DEFAULTS\n";
    static char l_back[]  = "BACK\n";
    char buf[48], msg[64];
    s32 i, n = sl_bedit_action_count();
    s32 dev = sl_bedit_device();
    s32 hl = !tab_prev_highlight;

    DL = viSetFillColor(DL, 0, 0, 0);
    DL = viFillScreen(DL);
    DL = frontSetupMenuBackground(DL);
    DL = microcode_constructor(DL);

    /* The device tabs: the selected one white and boxed, the other dimmed,
     * a hovered one boxed too. */
    DL = bnd_text(DL, BND_X_LABEL, BND_TAB_Y, (s8 *) l_kbm, dev == DEV_KBM ? 0xFF : SHADE_DIM,
                  dev == DEV_KBM || (hl && s_row == BND_ROW_TABS && s_col == 0));
    DL = bnd_text(DL, BND_X_TAB1, BND_TAB_Y, (s8 *) l_pad, dev == DEV_PAD ? 0xFF : SHADE_DIM,
                  dev == DEV_PAD || (hl && s_row == BND_ROW_TABS && s_col == 1));

    /* The header: the slot names, on both tabs. */
    DL = bnd_cell(DL, BND_X_VAL0, BND_HDR_Y, (s8 *) l_prim, SHADE_DIM, 0);
    DL = bnd_cell(DL, BND_X_VAL1, BND_HDR_Y, (s8 *) l_sec, SHADE_DIM, 0);

    /* The rows. */
    for (i = 0; i < n; i++)
    {
        s32 y = BND_ROW_Y(i);
        s32 on = hl && s_row == i;
        s32 s;

        DL = bnd_cell(DL, BND_X_LABEL, y, bnd_lf(buf, (s32) sizeof buf, sl_bedit_action_label(i)),
                      0xFF, on && s_col < 0);
        for (s = 0; s < 2; s++)
        {
            s32 cap = sl_bedit_slot_capturing(i, s);
            sl_bedit_slot_text(i, s, msg, (s32) sizeof msg);
            DL = bnd_cell(DL, s == 0 ? BND_X_VAL0 : BND_X_VAL1, y, bnd_lf(buf, (s32) sizeof buf, msg),
                          cap ? SHADE_ON : 0xFF, cap || (on && s_col == s));
        }
    }

    /* The footer. */
    DL = bnd_text(DL, BND_X_LABEL, BND_FOOT_Y, (s8 *) l_reset, 0xFF, hl && s_row == BND_ROW_FOOT && s_col == 0);
    DL = bnd_text(DL, BND_X_BACK, BND_FOOT_Y, (s8 *) l_back, 0xFF, hl && s_row == BND_ROW_FOOT && s_col == 1);

    /* The message line under the footer: a steal's TAKEN FROM <action> for
     * a couple of seconds. */
    if (sl_bedit_message()[0] != '\0')
        DL = bnd_cell(DL, BND_X_LABEL, BND_MSG_Y, bnd_lf(msg, (s32) sizeof msg, sl_bedit_message()), SHADE_ON, 0);

    DL = frontAddPreviousTabText(DL);
    DL = frontDrawCursor(DL);
    return DL;
}

#endif /* !__sgi */
