/**
 * sl_watch_pointer.c - the native pointer in the solo watch (#40): hover puts
 * the highlight under it, a left click acts on the item under it.
 *
 * WHY THIS IS NOT sl_menu_pointer.c. The front end is a pointer menu already:
 * one cursor, every screen hit-tests it, so the mouse only has to move that
 * cursor and a click is one N64 A edge. The watch is not. It is a STEPPED
 * menu (options.c:566-642): the stick moves an index, A/Z toggle a select
 * latch (watch_play_beep_sound, options.c:576), and left/right change the
 * latched item. It draws no cursor and hit-tests nothing. So a mouse in it
 * cannot be "the cursor"; it has to be a hit test against what the page draws
 * and a direct write of the same state the stick path writes:
 *
 *     watch_screen_index          the page                options.c:87
 *     game_options_index          the Game Options row    options.c:89
 *     controller_options_index    the Control Options row options.c:88
 *     sl_sightline_row_index      the SIGHTLINE row (#44) options.c, native
 *     watch_item_is_actively_selected   the A/Z latch     options.c:96
 *     D_800409A4                  abort: confirm/cancel   options.c:94
 *     watch_inventory_cursor_pos  the equipment list      options.c:106
 *
 * There is no second highlight and no second selection model: hover writes
 * the index the keyboard writes, so the two share one state and either can
 * take over at any moment (the mouse only acts on MOTION - the same serial
 * rule as the front end - so a stationary mouse never fights W/S).
 *
 * WHERE THE RECTANGLES COME FROM. Nothing here is a measured pixel. The text
 * rows are hit-tested at the x/y the page passes to textRender and
 * draw_options_labels, in FRAMEBUFFER space (textrelated.c:311 - texrects at
 * x*4, y*4 with text_x/text_y = 0), with the width the game's own textMeasure
 * returns for the same string and line height. The 3D elements - the six
 * page-select rectangles (options.c draw_background_health_and_armor's
 * native bar, setup_watch_rectangles with SL_WATCH_SCREEN_SELECT_* from
 * options.h - #44 added the SIGHTLINE page) and the two volume tracks
 * (options.c draw_music_volume_slider / draw_fx_volume_slider, 600x20 at
 * z -275 / -205) - are projected through the SAME matrices the RSP is handed:
 * the page matrix draw_watch_current_page receives, the two guScale matrices
 * draw_background_health_and_armor multiplies in (gfx_background_8007B0A0 /
 * _8007B0E0), and bondviewRenderWatch's guPerspective (bondview2.c:8939:
 * zoominfovy, aspect 1.4545455, near 10, far 300), then the player's viewport
 * (fr.c:698-701: vscale/vtrans from viGetView*). So a page zoom, a different
 * screen-size option or a window resize all follow with no edit here.
 *
 * THE POINTER arrives as the platform's absolute sample mapped onto the
 * presented image by sl_menu_pointer_uv (sl_menu_pointer.c) - the one mapping
 * the front end's cursor uses - and then onto the framebuffer (viGetX/Y),
 * which is the space every texrect and the viewport are expressed in.
 *
 * THE CLICK RULES, one line each (the same for every page):
 *   a LABEL           = what A does on that row: highlight it and toggle the
 *                       select latch (keyboard left/right then edit it)
 *   a VALUE           = set that value, directly, with the row's own confirm
 *                       sound (game_option_select_value) - no latch needed
 *   a volume TRACK    = set the volume at the click, and follow the pointer
 *                       while the button stays down
 *   a SIGHTLINE slider bar (#50) = the track rule on the row's own value:
 *                       set at the click (snapped to the value's step grid,
 *                       options.c sl_sightline_slide), follow while held
 *   a PAGE rectangle  = go to that page, with the page's own sound and zoom
 *   an equipment ROW  = select it and equip it (through the A/Z/START lines,
 *                       sub_GAME_7F0A8378, once the list has settled on it)
 *   a LAYOUT name     = the next BUTTON / STICK layout on the Control Options
 *                       page (#63, sl_watch_layout_step; it was the next
 *                       control style until the styles left on 2026-09-20)
 *   abort: / confirm / cancel = A on abort:, then confirm aborts, cancel
 *                       clears - the pad's two presses, as two clicks
 *   anything else     = nothing
 *
 * NOT here: no keyboard shortcut, no synthesised button, no change to any
 * stick path, and nothing at all for the IDO build - the whole file is
 * !__sgi and options.c carries two guarded calls into it.
 */
#ifndef __sgi

/* Host stdio/stdlib FIRST, before the N64 tree can shadow them - the order
 * every other src/native file that prints uses (sl_asset_override.c:23). */
#include <stdio.h>
#include <stdlib.h>
#include <ultra64.h>
#include <bondgame.h>
#include <boss.h>
#include <fr.h>
#include <music.h>
#include <snd.h>
#include <PR/gu.h>
#include "bondview.h"
#include "player.h"
#include "options.h"
#include "textrelated.h"
#include "language.h"
#include "bondinv.h"
#include "gun.h"
#include "file.h"
#include "front.h"
#include "mp_music.h"
#include "debugmenu_handler.h"
#include "image_bank.h"
#include "othermodemicrocode.h"
#include "bondwalk2.h"
#include "assets/obseg/text/LoptionE.h"

/* The shared pointer mapping (sl_menu_pointer.c), whether the pointer is
 * captured (src/platform/sl_input.c), the one Invert Mouse Y state (#39) and
 * the one Sprint setter (sl_settings_apply.c, #42) - the SIGHTLINE page's two
 * rows (#44) are views onto these, through the same calls options.c makes. */
extern int  sl_menu_pointer_uv(f32 *u, f32 *v, unsigned int *motion);
extern int  sl_input_pointer_captured(void);
extern int  sl_input_pointer_probed(void);   /* SL_POINTER_PROBE: draw for a probed pointer too */
/* The crosshair's placement across the CONTENT rect (the widescreen cursor
 * repair): the platform's pointer, the renderer's two rectangles and the
 * pure extended mapping - the same three the front end's cursor uses
 * (sl_menu_pointer.c sl_menu_cursor_tag). */
extern int  sl_input_pointer_get(int *x, int *y, int *win_w, int *win_h, unsigned int *motion_serial);
extern int  sl_gfx_present_rect(int win_h, int *x, int *y, int *w, int *h);
extern int  sl_gfx_content_rect(int win_h, int *x, int *y, int *w, int *h);
extern int  sl_display_pointer_logical(const int safe[4], const int content[4],
                                       int px, int py, float scr_w, float scr_h,
                                       float *lx, float *ly);
extern int  sl_mouse_invert_y_get(void);
extern void sl_mouse_invert_y_set(int on);
extern int  sl_sprint_enabled(void);
extern void sl_sprint_enabled_set(int on);
extern int  sl_action_mode(int which);      /* #56: 0 CROUCH / 1 SPRINT -> 0 hold / 1 toggle */
/* The #45 / #50 values, for the witness lines only (the rows themselves are
 * options.c's: a click here goes through sl_sightline_click / _slide). */
extern int  sl_aspect_ratio(void);
extern const char *sl_aspect_name(int);
extern int  sl_fov_h16_displayed(void);
extern int  sl_mouse_sens_get(int scoped);

/* THE CURSOR: the game's own aiming reticle, drawn the way gunDrawSight
 * (gunfire.c:6329-6353) draws it - texSelect on crosshairimage, then
 * display_image_at_position with a half-extent - at this fraction of the
 * sight's 16-unit half-extent. Owner request 2026-09-17: a SMALLER copy of
 * the in-game crosshair, no new art. Half size. */
#define SL_WATCH_CURSOR_SCALE 0.5f
#define SL_WATCH_CURSOR_ALPHA 0xFF   /* the sight uses 0x6E over the world;
                                        opaque reads better on the green face */

/* options.c state and helpers that have no header prototype. All non-static
 * there; declared here rather than in a decomp header this file has no
 * business editing. */
extern u32 watch_screen_index;
extern u32 controller_options_index;
extern u32 game_options_index;
extern u32 sl_sightline_row_index;     /* the SIGHTLINE page's row (#44) */
extern void sl_reset_sightline_row_index(void);
/* The SIGHTLINE page's nested BINDINGS child (#46, sl_watch_bindings.c):
 * while open it owns the page's rows - the pointer hovers and clicks ITS
 * rows through these, and leaving the page closes it. */
extern s32  sl_watch_bindings_is_open(void);
extern void sl_watch_bindings_open(void);
extern void sl_watch_bindings_close(void);
extern s32  sl_watch_bindings_visible(s32 *top, s32 *count);
extern void sl_watch_bindings_row_text(s32 row, char *label, char *v0, char *v1, s32 n);
extern void sl_watch_bindings_hover(s32 row);
extern void sl_watch_bindings_click(s32 row, s32 value);
extern s32 watch_item_is_actively_selected;
extern s32 D_800409A4;                 /* abort: 1 = confirm highlighted */
extern s32 g_curWatchItemIndex;
extern s32 watch_inventory_text_y;     /* the list's drawn y offset */
extern f32 watch_inventory_cursor_pos;
/* The Control Options page's two LAYOUT rows (#63, sl_watch_controller.c). */
extern const char *sl_watch_layout_name(s32 which);
extern void        sl_watch_layout_step(s32 which, s32 dir);
/* The CONTROLS child's NAMED value rows (options.c): the name drawn
 * right-aligned at SL_SIGHTLINE_X_BARVALUE, lowercased. */
extern const char *sl_sightline_row_value_name(s32 row);
extern Mtx gfx_background_8007B0A0;    /* guScale 0.25 (options.c:1770) */
extern Mtx gfx_background_8007B0E0;    /* guScale 1,1,scale (options.c:1786) */
extern void game_option_select_value(u32 *param_1, u32 param_2);
extern void sub_GAME_7F0A5210(void);   /* page-change beep + static chance */
extern void watch_play_beep_sound(void);
extern void reset_watch_item_is_actively_selected(void);
extern void set_controlstick_lr_disabled(void);
extern void disable_watch_stick_y_nav_ready(void);
extern u16 *bondinvGetNameByIndex(s32 index);

/* The watch's own perspective (bondview2.c:8937-8939): the aspect is the
 * literal the game passes beside g_CurrentPlayer->zoominfovy; near and far
 * likewise. EU passes a different aspect on the same line. */
#if defined(VERSION_EU)
#define SL_WATCH_ASPECT 1.4005603f
#else
#define SL_WATCH_ASPECT 1.4545455f
#endif
#define SL_WATCH_ZNEAR 10.0f
#define SL_WATCH_ZFAR  300.0f

/* The volume tracks' geometry: setup_watch_rectangles(vtx, 0, 0, 600, 20,
 * -299, -275) for music and (..., -205) for fx (options.c:2747, :2696). The
 * width and x start are the page bar's WATCH_SCREEN_SELECT_* values. */
#define SL_TRACK_X0      WATCH_SCREEN_SELECT_RECTANGLE_MIN_X
#define SL_TRACK_W       WATCH_SCREEN_SELECT_TOTAL_WIDTH
#define SL_TRACK_H       20
#define SL_TRACK_Z_MUSIC (-275)
#define SL_TRACK_Z_FX    (-205)

/* The page bar's geometry (options.c draw_background_health_and_armor, the
 * native six-segment bar - the same call shape as bondview2.c:3800):
 * setup_watch_rectangles(vtx, i, 0, SL_WATCH_SCREEN_SELECT_RECTANGLE_WIDTH,
 * 0x14, -0x12B, 0x136), i stepping by SL_WATCH_SCREEN_SELECT_RECTANGLE_HSTEP. */
#define SL_BAR_X0  (-0x12B)
#define SL_BAR_Z0  0x136
#define SL_BAR_W   SL_WATCH_SCREEN_SELECT_RECTANGLE_WIDTH
#define SL_BAR_H   0x14

/* Padding, in framebuffer pixels, around every target: the tracks and the
 * bar are 6 and 5 pixels tall on screen and a row of text 10. */
#define SL_PAD 2.0f

/* What the pointer is over. */
enum {
    HIT_NONE = 0,
    HIT_PAGE,       /* row = page */
    HIT_LABEL,      /* row = the page's row index */
    HIT_VALUE,      /* row, value */
    HIT_TRACK,      /* row = 0 music / 1 fx, t = 0..1 along it */
    HIT_ITEM,       /* row = equipment list index */
    HIT_STYLE,      /* the value beside a Control Options row (a layout name; the style name until #63) */
    HIT_ABORT,
    HIT_CONFIRM,
    HIT_CANCEL,
    HIT_SLIDER      /* a SIGHTLINE slider row's bar (#50): row, t = 0..1 along it */
};

struct hit {
    int kind;
    s32 row;
    s32 value;
    f32 t;
};

/* THE ONE DIAGNOSTIC: under SL_INPUT_DEBUG (the same knob as the pad line in
 * sl_input.c) print the pointer in framebuffer pixels, what it is over and the
 * watch state it acts on - once per change of hit, and on every click. */
static int debug_on(void)
{
    static int on = -1;
    if (on < 0) on = getenv("SL_INPUT_DEBUG") != NULL;
    return on;
}
static const char *kind_name(int kind)
{
    static const char *names[] = { "none", "page", "label", "value", "track",
                                   "item", "style", "abort", "confirm", "cancel", "slider" };
    return (kind >= 0 && kind < 11) ? names[kind] : "?";
}
static void debug_line(const char *tag, const struct hit *h, f32 px, f32 py);

/* Per-frame state. */
static unsigned int s_last_motion;
static int          s_have_motion;
static int          s_clicks;          /* LMB down edges not yet applied */
static int          s_lmb_down;
static int          s_drag_track = -1; /* which track a held button follows */
static int          s_drag_slider = -1;/* which SIGHTLINE slider row a held button follows (#50) */
static s32          s_equip_pending;   /* 1 + the clicked list index, or 0 */

/* From the platform (sl_input.c), on the SDL thread's event pump. Returns 1
 * when the click is the watch's - i.e. the watch is up and interactive
 * (state 5, the only state in which the frame hook runs) - and 0 otherwise,
 * so a click during the level-start watch animation or the open/close
 * transitions is not banked and applied to whatever the page shows once it
 * arrives (measured: the arming click at level start landed on the centre of
 * the first page opened, seconds later). */
s32 sl_watch_pointer_click(void)
{
    if (g_CurrentPlayer == NULL
        || g_CurrentPlayer->watch_animation_state != WATCH_ANIMATION_0x5)
        return 0;
    s_clicks++;
    s_lmb_down = 1;
    return 1;
}

void sl_watch_pointer_release(void)
{
    s_lmb_down = 0;
    s_drag_track = -1;
    s_drag_slider = -1;
}

/* For sub_GAME_7F0A8378: did a click ask for the item the list settled on? */
s32 sl_watch_pointer_take_equip(void)
{
    if (s_equip_pending != 0 && s_equip_pending - 1 == g_curWatchItemIndex)
    {
        s_equip_pending = 0;
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------------
 * Geometry
 * --------------------------------------------------------------------- */

/* Row-vector transform, the N64's convention: out = v * m. */
static void xform(const f32 v[4], f32 m[4][4], f32 out[4])
{
    s32 i;
    for (i = 0; i < 4; i++)
        out[i] = v[0] * m[0][i] + v[1] * m[1][i] + v[2] * m[2][i] + v[3] * m[3][i];
}

/* Project a point on the watch face (x, 0, z in setup_watch_rectangles units)
 * to framebuffer pixels through the same chain the RSP runs for it. */
static int watch_project(Mtx *pagemtx, f32 x, f32 z, f32 *sx, f32 *sy)
{
    f32 mv[4][4], s1[4][4], s2[4][4], p[4][4];
    f32 v[4], a[4], b[4], c[4], d[4];
    u16 pn;
    f32 nx, ny;

    if (g_CurrentPlayer == NULL)
        return 0;

    guMtxL2F(mv, pagemtx);
    guMtxL2F(s1, &gfx_background_8007B0A0);
    guMtxL2F(s2, &gfx_background_8007B0E0);
    guPerspectiveF(p, &pn, g_CurrentPlayer->zoominfovy, SL_WATCH_ASPECT,
                   SL_WATCH_ZNEAR, SL_WATCH_ZFAR, 1.0f);

    v[0] = x; v[1] = 0.0f; v[2] = z; v[3] = 1.0f;
    xform(v, s1, a);        /* gSPMatrix MUL order: the scales apply first */
    xform(a, s2, b);
    xform(b, mv, c);
    xform(c, p, d);
    if (d[3] <= 0.0001f)
        return 0;
    nx = d[0] / d[3];
    ny = d[1] / d[3];

    /* The viewport fr.c:698-701 builds: centre + ndc * half-size, y down. */
    *sx = (f32) viGetViewLeft() + (f32) viGetViewWidth()  * (1.0f + nx) * 0.5f;
    *sy = (f32) viGetViewTop()  + (f32) viGetViewHeight() * (1.0f - ny) * 0.5f;
    return 1;
}

/* Project an axis-aligned face rectangle to a framebuffer rectangle
 * (min/max of its four corners). */
static int watch_project_rect(Mtx *pagemtx, f32 x0, f32 z0, f32 w, f32 h,
                              f32 *l, f32 *t, f32 *r, f32 *b)
{
    f32 cx[4], cy[4];
    s32 i;
    if (!watch_project(pagemtx, x0,     z0,     &cx[0], &cy[0])) return 0;
    if (!watch_project(pagemtx, x0 + w, z0,     &cx[1], &cy[1])) return 0;
    if (!watch_project(pagemtx, x0,     z0 + h, &cx[2], &cy[2])) return 0;
    if (!watch_project(pagemtx, x0 + w, z0 + h, &cx[3], &cy[3])) return 0;
    *l = cx[0]; *r = cx[0]; *t = cy[0]; *b = cy[0];
    for (i = 1; i < 4; i++)
    {
        if (cx[i] < *l) *l = cx[i];
        if (cx[i] > *r) *r = cx[i];
        if (cy[i] < *t) *t = cy[i];
        if (cy[i] > *b) *b = cy[i];
    }
    return 1;
}

/* The face z whose projection lands on framebuffer y (the SIGHTLINE page's
 * slider bars, options.c sl_draw_sightline_bar, #50): the inverse of
 * watch_project along x = 0. The face is drawn flat-on - measured 2026-09-19
 * at WATCHZOOM3, the projected y is the same at x -299, 0 and 301 for every
 * z, and linear in z (z -275 -> 32.33, 0 -> 120.00, 310 -> 218.83: 0.3188
 * px per unit) - so two projections fix the line and the inverse is exact.
 * Falls back to that measured line if the projection degenerates. */
f32 sl_watch_face_z_at_fb_y(Mtx *pagemtx, f32 y)
{
    f32 x0, y0, x1, y1;
    if (pagemtx != NULL
        && watch_project(pagemtx, 0.0f, 0.0f, &x0, &y0)
        && watch_project(pagemtx, 0.0f, 100.0f, &x1, &y1)
        && (y1 - y0 > 0.001f || y1 - y0 < -0.001f))
        return (y - y0) * 100.0f / (y1 - y0);
    return (y - 120.0f) / 0.3188f;
}

static int in_rect(f32 px, f32 py, f32 l, f32 t, f32 r, f32 b)
{
    return px >= l - SL_PAD && px <= r + SL_PAD && py >= t - SL_PAD && py <= b + SL_PAD;
}

/* The horizontal extent of a string drawn as draw_options_labels /
 * textRender draw it: left-aligned at x, centred on x, or right-aligned to x,
 * measured with the game's own textMeasure at the same line height. */
static void text_extent(s32 x, char *text, s32 lineheight, s32 centre, s32 rightalign,
                        f32 *l, f32 *r, s32 *height)
{
    s32 h = 0, w = 0, tx;
    textMeasure(&h, &w, text, ptrFontBankGothicChars, ptrFontBankGothic, lineheight);
    if (centre)         tx = x - w / 2;
    else if (rightalign) tx = x - w;
    else                 tx = x;
    *l = (f32) tx;
    *r = (f32) (tx + w);
    *height = h;
}

static int text_hit(f32 px, f32 py, s32 x, s32 y, char *text, s32 lineheight,
                    s32 centre, s32 rightalign)
{
    f32 l, r;
    s32 h;
    text_extent(x, text, lineheight, centre, rightalign, &l, &r, &h);
    return in_rect(px, py, l, (f32) y, r, (f32) (y + h));
}

/* ------------------------------------------------------------------------
 * Hit tests, one per page, against exactly what that page draws
 * --------------------------------------------------------------------- */

/* The page bar is drawn by every page (draw_background_health_and_armor). */
static int hit_page_bar(Mtx *pagemtx, f32 px, f32 py, struct hit *out)
{
    s32 i;
    f32 l, t, r, b;
    for (i = 0; i < SL_WATCH_NUMBER_SCREENS; i++)
    {
        if (!watch_project_rect(pagemtx,
                                (f32) (SL_BAR_X0 + i * SL_WATCH_SCREEN_SELECT_RECTANGLE_HSTEP),
                                (f32) SL_BAR_Z0, (f32) SL_BAR_W, (f32) SL_BAR_H,
                                &l, &t, &r, &b))
            return 0;
        if (in_rect(px, py, l, t, r, b))
        {
            out->kind = HIT_PAGE;
            out->row = i;
            return 1;
        }
    }
    return 0;
}

/* Game Options (draw_watch_game_options_page, draw_toggle_options,
 * draw_toggle_option_values). */
static void hit_game_options(Mtx *pagemtx, f32 px, f32 py, struct hit *out)
{
    s32 i, h;
    f32 l, t, r, b;

    /* music / fx labels at XOFFSET_1, YOFFSET_8 / YOFFSET_9 (options.c). */
    if (text_hit(px, py, XOFFSET_1, YOFFSET_8,
                 (char *) langGet(getStringID(LOPTIONS, OPTION_STR_35_MUSIC_LF)), 0, 0, 0))
    { out->kind = HIT_LABEL; out->row = GAME_OPTIONS_INDEX_MUSIC; return; }
    if (text_hit(px, py, XOFFSET_1, YOFFSET_9,
                 (char *) langGet(getStringID(LOPTIONS, OPTION_STR_36_FX_LF)), 0, 0, 0))
    { out->kind = HIT_LABEL; out->row = GAME_OPTIONS_INDEX_FX; return; }

    /* The two tracks. t is the fraction along the track's projected width,
     * which is what the fill (update_volume_slider_verts) is drawn against. */
    for (i = 0; i < 2; i++)
    {
        if (watch_project_rect(pagemtx, (f32) SL_TRACK_X0,
                               (f32) (i == 0 ? SL_TRACK_Z_MUSIC : SL_TRACK_Z_FX),
                               (f32) SL_TRACK_W, (f32) SL_TRACK_H, &l, &t, &r, &b)
            && in_rect(px, py, l, t, r, b) && r > l)
        {
            out->kind = HIT_TRACK;
            out->row = i;
            out->t = (px - l) / (r - l);
            if (out->t < 0.0f) out->t = 0.0f;
            if (out->t > 1.0f) out->t = 1.0f;
            return;
        }
    }

    /* The toggle rows: the 8 table rows, each YINC tall from YOFFSET_1
     * (draw_toggle_options' loop). A row band starts one SL_PAD above its
     * text so the gap between rows belongs to the row below, which is where
     * the text sits. */
    for (i = 0; i < 8; i++)
    {
        s32 y = YOFFSET_1 + i * YINC;
        s32 row = GAME_OPTIONS_INDEX_LOOK_UPDOWN + i;
        s32 nvalues, j;
        s32 x1, x2, x3;
        char *label, *vtext[3];
        f32 right = 0.0f;
        struct game_options *e = &game_options_entries[i];

        if (py < (f32) y - SL_PAD || py >= (f32) (y + YINC) - SL_PAD)
            continue;

        label = (char *) langGet(e->text[0]);
        vtext[0] = (char *) langGet(e->text[1]);
        vtext[1] = (char *) langGet(e->text[2]);
        vtext[2] = e->text[3] ? (char *) langGet(e->text[3]) : NULL;
        nvalues = e->text[3] ? 3 : 2;

        /* Value columns exactly as draw_toggle_option_values places them:
         * three-value rows at 0xB4 / 0xE1 / 0x10E, two-value rows at 0xC8 /
         * 0xFA (JP text: 0xAA / 0xDC and 0xBE / 0xFA), all centred. */
        if (nvalues == 3) { x1 = j_text_trigger ? 0xAA : 0xB4; x2 = j_text_trigger ? 0xDC : 0xE1; x3 = 0x10E; }
        else              { x1 = j_text_trigger ? 0xBE : 0xC8; x2 = 0xFA; x3 = 0; }

        text_extent(XOFFSET_1, label, 10, 0, 0, &l, &r, &h);
        if (px >= l - SL_PAD && px <= r + SL_PAD)
        { out->kind = HIT_LABEL; out->row = row; return; }

        for (j = 0; j < nvalues; j++)
        {
            s32 cx = (j == 0) ? x1 : (j == 1) ? x2 : x3;
            text_extent(cx, vtext[j], 10, 1, 0, &l, &r, &h);
            if (r > right) right = r;
            if (px >= l - SL_PAD && px <= r + SL_PAD)
            { out->kind = HIT_VALUE; out->row = row; out->value = j; return; }
        }

        /* In the row but on no target: hover still lands on the row. */
        if (px >= (f32) XOFFSET_1 - SL_PAD && px <= right + SL_PAD)
        { out->kind = HIT_NONE; out->row = row; return; }
        return;
    }
}

/* A SIGHTLINE slider row's bar (#50, options.c sl_draw_sightline_bar): the
 * tracks' x extent at the face z under the row's bar line - the label's y
 * less SL_SIGHTLINE_BAR_ABOVE through the same inverse the draw uses - as a
 * framebuffer rectangle through the same projection as the volume tracks. */
static int sightline_bar_rect(Mtx *pagemtx, s32 row, f32 *l, f32 *t, f32 *r, f32 *b)
{
    f32 z = sl_watch_face_z_at_fb_y(pagemtx, (f32) (sl_sightline_row_y(row) - SL_SIGHTLINE_BAR_ABOVE));
    return watch_project_rect(pagemtx, (f32) SL_SIGHTLINE_BAR_X0, (f32) (s32) z,
                              (f32) SL_SIGHTLINE_BAR_W, (f32) SL_SIGHTLINE_BAR_H, l, t, r, b)
           && *r > *l;
}

/* The top of a row's hit band: SL_PAD above its label, or above its bar when
 * it is a slider row (the bar's line belongs to the row it fills). */
static f32 sightline_band_top(s32 row)
{
    s32 y = sl_sightline_row_y(row);
    if (SL_WROW_IS_SLIDER(sl_sightline_row_kind(row)))
        y -= SL_SIGHTLINE_BAR_ABOVE;
    return (f32) y - SL_PAD;
}

/* SIGHTLINE (#44, sl_draw_watch_sightline_page): the heading at XOFFSET_1,
 * SL_SIGHTLINE_HEADING_Y is not a target; the rows at XOFFSET_1,
 * sl_sightline_row_y(i) with the two-value columns of a Game Options toggle
 * (0xC8 / 0xFA, JP 0xBE / 0xFA, centred). A row's band runs from its top
 * (sightline_band_top) to the next row's, the last row's YINC below its
 * label; a slider row's band holds its bar too (#50). */
static void hit_sightline(Mtx *pagemtx, f32 px, f32 py, struct hit *out)
{
    static char cell_minus[]   = "-\n";
    static char cell_plus[]    = "+\n";
    static char cell_value[]   = "32:9\n";      /* the widest `-` value `+` cell (ASPECT RATIO), for the extent */
    static char bar_value[]    = "100%\n";      /* the widest slider value, for the extent */
    char named[20];
    s32 i, j, h, kind;
    s32 count = sl_sightline_row_count();
    f32 l, r;

    /* The view in force's rows (options.c sl_watch_views, #45): the cells
     * follow the row's KIND - a toggle's off / on at the toggles' columns, a
     * value row's `-` / value / `+` at SL_SIGHTLINE_X_*, a slider row its
     * BAR (t = the fraction along it) and its label, an action row
     * (sub-menu, BACK, BINDINGS) its label only, a DIMMED row nothing. */
    for (i = 0; i < count; i++)
    {
        s32 y = sl_sightline_row_y(i);
        s32 x1 = j_text_trigger ? 0xBE : 0xC8;
        s32 x2 = 0xFA;
        char *label = (char *) sl_sightline_row_label(i);
        char *vtext[2];
        f32 right = 0.0f;
        f32 top = sightline_band_top(i);
        f32 bottom = i + 1 < count ? sightline_band_top(i + 1) : (f32) (y + YINC) - SL_PAD;

        if (py < top || py >= bottom)
            continue;
        kind = sl_sightline_row_kind(i);
        if (SL_WROW_IS_INERT(kind))
            return;

        if (SL_WROW_IS_SLIDER(kind))
        {
            f32 bl, bt, br, bb;
            if (sightline_bar_rect(pagemtx, i, &bl, &bt, &br, &bb) && in_rect(px, py, bl, bt, br, bb))
            {
                out->kind = HIT_SLIDER;
                out->row = i;
                out->t = (px - bl) / (br - bl);
                if (out->t < 0.0f) out->t = 0.0f;
                if (out->t > 1.0f) out->t = 1.0f;
                return;
            }
            text_extent(XOFFSET_1, label, 10, 0, 0, &l, &r, &h);
            if (px >= l - SL_PAD && px <= r + SL_PAD)
            { out->kind = HIT_LABEL; out->row = i; return; }
            text_extent(SL_SIGHTLINE_X_BARVALUE, bar_value, 10, 0, 1, &l, &r, &h);
            if (px >= (f32) XOFFSET_1 - SL_PAD && px <= r + SL_PAD)
            { out->kind = HIT_NONE; out->row = i; return; }
            return;
        }

        /* The toggle rows' two cells: off / on, or hold / toggle for the
         * #56 mode rows (options.c sl_draw_sightline_toggle prints the
         * same strings at the same columns). The ids are chosen before
         * getStringID, whose slot argument is not parenthesised. */
        {
            s32 id_lo = SL_WROW_IS_MODE(kind) ? OPTION_STR_1E_HOLD_LF   : OPTION_STR_1A_OFF_LF;
            s32 id_hi = SL_WROW_IS_MODE(kind) ? OPTION_STR_1D_TOGGLE_LF : OPTION_STR_19_ON_LF;
            vtext[0] = (char *) langGet(getStringID(LOPTIONS, id_lo));
            vtext[1] = (char *) langGet(getStringID(LOPTIONS, id_hi));
        }

        text_extent(XOFFSET_1, label, 10, 0, 0, &l, &r, &h);
        if (px >= l - SL_PAD && px <= r + SL_PAD)
        { out->kind = HIT_LABEL; out->row = i; return; }

        if (kind == SL_WROW_SUBMENU || kind == SL_WROW_BACK || kind == SL_WROW_BINDINGS)
        { out->kind = HIT_NONE; out->row = i; return; }

        if (SL_WROW_IS_NAMED(kind))
        {
            /* The layout rows (#63) and the #52 WINDOW MODE / RESOLUTION
             * rows: the name, right-aligned at the slider rows' value
             * column, is the one cell - a click steps up (value 2, the `+`
             * of a value row). */
            const char *nm = sl_sightline_row_value_name(i);
            for (j = 0; nm[j] != '\0' && j < 17; j++) named[j] = (char) ((nm[j] >= 'A' && nm[j] <= 'Z') ? nm[j] + 32 : nm[j]);
            named[j++] = '\n'; named[j] = '\0';
            text_extent(SL_SIGHTLINE_X_BARVALUE, named, 10, 0, 1, &l, &r, &h);
            if (px >= l - SL_PAD && px <= r + SL_PAD)
            { out->kind = HIT_VALUE; out->row = i; out->value = 2; return; }
            if (px >= (f32) XOFFSET_1 - SL_PAD && px <= r + SL_PAD)
            { out->kind = HIT_NONE; out->row = i; return; }
            return;
        }

        if (SL_WROW_IS_VALUE(kind))
        {
            static const s32 cx[3] = { SL_SIGHTLINE_X_MINUS, SL_SIGHTLINE_X_VALUE, SL_SIGHTLINE_X_PLUS };
            for (j = 0; j < 3; j++)
            {
                text_extent(cx[j], j == 0 ? cell_minus : j == 1 ? cell_value : cell_plus, 10, 1, 0, &l, &r, &h);
                if (r > right) right = r;
                if (px >= l - SL_PAD && px <= r + SL_PAD)
                { out->kind = HIT_VALUE; out->row = i; out->value = j; return; }
            }
            if (px >= (f32) XOFFSET_1 - SL_PAD && px <= right + SL_PAD)
            { out->kind = HIT_NONE; out->row = i; return; }
            return;
        }

        for (j = 0; j < 2; j++)
        {
            text_extent(j == 0 ? x1 : x2, vtext[j], 10, 1, 0, &l, &r, &h);
            if (r > right) right = r;
            if (px >= l - SL_PAD && px <= r + SL_PAD)
            { out->kind = HIT_VALUE; out->row = i; out->value = j; return; }
        }

        if (px >= (f32) XOFFSET_1 - SL_PAD && px <= right + SL_PAD)
        { out->kind = HIT_NONE; out->row = i; return; }
        return;
    }
}

/* The BINDINGS child (#46, sl_watch_bindings_draw): the visible rows from
 * SL_WATCH_BINDINGS_ROW_Y(0), YINC apart, label at XOFFSET_1, the two values
 * left-aligned at SL_WATCH_BINDINGS_X_VALUE (one per action row) or X_V0 / X_V1
 * (the two-value rows), options.h; the heading
 * line and the message are not targets. row = the LIST index (top + i). */
static void hit_bindings(f32 px, f32 py, struct hit *out)
{
    char label[48], v0[48], v1[48];
    s32 top = 0, count = 0, i, j, h;
    f32 l, r;

    if (!sl_watch_bindings_visible(&top, &count))
        return;
    for (i = 0; i < SL_WATCH_BINDINGS_VISIBLE && top + i < count; i++)
    {
        s32 y = SL_WATCH_BINDINGS_ROW_Y(i);
        s32 row = top + i;
        f32 right = 0.0f;
        char *vtext[2];

        if (py < (f32) y - SL_PAD || py >= (f32) (y + YINC) - SL_PAD)
            continue;
        sl_watch_bindings_row_text(row, label, v0, v1, (s32) sizeof label);
        vtext[0] = v0; vtext[1] = v1;

        text_extent(XOFFSET_1, label, 10, 0, 0, &l, &r, &h);
        if (px >= l - SL_PAD && px <= r + SL_PAD)
        { out->kind = HIT_LABEL; out->row = row; return; }
        for (j = 0; j < 2; j++)
        {
            s32 vx;
            if (vtext[j][0] == '\0') continue;
            /* an action row has one value at X_VALUE; the two-value rows
             * sit at X_V0 / X_V1 (options.h) */
            vx = (v1[0] == '\0') ? SL_WATCH_BINDINGS_X_VALUE
               : (j == 0 ? SL_WATCH_BINDINGS_X_V0 : SL_WATCH_BINDINGS_X_V1);
            text_extent(vx, vtext[j], 10, 0, 0, &l, &r, &h);
            if (r > right) right = r;
            if (px >= l - SL_PAD && px <= r + SL_PAD)
            { out->kind = HIT_VALUE; out->row = row; out->value = j; return; }
        }
        if (px >= (f32) XOFFSET_1 - SL_PAD && px <= (right > 0.0f ? right : r) + SL_PAD)
        { out->kind = HIT_NONE; out->row = row; return; }
        return;
    }
}

/* Control Options (#63, 2026-09-20 - sl_watch_controller.c
 * sl_watch_layout_rows_draw: "button layout" at XOFFSET_1,0x1A and "stick
 * layout" at XOFFSET_1,0x2B, each value at 0xAA on its row). A click on a
 * value steps it (HIT_STYLE, the original page's "the style name" cell,
 * kept as the kind for "the value beside the row"). */
static void hit_control_options(f32 px, f32 py, struct hit *out)
{
    static char l_button[] = "button layout\n";
    static char l_stick[]  = "stick layout\n";
    char value[20];
    s32 row, i;

    for (row = 0; row < 2; row++)
    {
        s32 y = row == 0 ? 0x1A : 0x2B;
        const char *n = sl_watch_layout_name(row);
        for (i = 0; n[i] != '\0' && i < 17; i++) value[i] = (char) ((n[i] >= 'A' && n[i] <= 'Z') ? n[i] + 32 : n[i]);
        value[i++] = '\n'; value[i] = '\0';
        if (text_hit(px, py, XOFFSET_1, y, row == 0 ? l_button : l_stick, 0, 0, 0))
        { out->kind = HIT_LABEL; out->row = row; return; }
        if (text_hit(px, py, 0xAA, y, value, 0, 0, 0))
        { out->kind = HIT_STYLE; out->row = row; return; }
    }

    /* The two row bands, for hover only. */
    if (py >= 0x1A - SL_PAD && py < 0x2B - SL_PAD && px >= XOFFSET_1 - SL_PAD)
    { out->kind = HIT_NONE; out->row = CONTROLLER_OPTIONS_INDEX_STYLE; return; }
    if (py >= 0x2B - SL_PAD && py < 0x2B + 10 + SL_PAD && px >= XOFFSET_1 - SL_PAD)
    { out->kind = HIT_NONE; out->row = CONTROLLER_OPTIONS_INDEX_INPUTS; return; }
}

/* Mission Status (draw_abort_cancel_confirm: abort: at 0x51, confirm at 0xBD,
 * cancel at 0x88, all on one row at 0x4C (PAL 0x4E), JP text shifted). */
static void hit_mission_status(f32 px, f32 py, struct hit *out)
{
    s32 x_abort   = 0x51;
    s32 x_confirm = (j_text_trigger ? 0xF : 0) + 0xBD;
    s32 x_cancel  = (j_text_trigger ? 0xA : 0) + 0x88;
    s32 y         = (j_text_trigger ? 3 : 0) + (PAL ? 0x4E : 0x4C);

    if (text_hit(px, py, x_abort, y,
                 (char *) langGet(getStringID(LOPTIONS, OPTION_STR_24_ABORT_LF)), 0, 0, 0))
    { out->kind = HIT_ABORT; return; }
    if (text_hit(px, py, x_confirm, y,
                 (char *) langGet(getStringID(LOPTIONS, OPTION_STR_25_CONFIRM_LF)), 0, 0, 0))
    { out->kind = HIT_CONFIRM; return; }
    if (text_hit(px, py, x_cancel, y,
                 (char *) langGet(getStringID(LOPTIONS, OPTION_STR_26_CANCEL_LF)), 0, 0, 0))
    { out->kind = HIT_CANCEL; return; }
}

/* Equipment (draw_watch_inventory_page): the list is one string of all the
 * item names, drawn at x 0x4E from y WATCH_INV_BASE_Y with the DRAWN offset
 * watch_inventory_text_y and one line per item, clipped to five lines; the
 * highlight box runs from x 0x4B to textwidth + 0x52. Row i is therefore at
 * base + text_y + i * lineheight, and only the clipped window counts. */
static void hit_equipment(f32 px, f32 py, struct hit *out)
{
    s32 lineheight = j_text_trigger ? 14 : 12;
#if defined(VERSION_EU)
    s32 base_y = j_text_trigger ? 0x82 : 0xAA;
#elif defined(VERSION_JP)
    s32 base_y = j_text_trigger ? 0x82 : 0x8C;
#else
    s32 base_y = 0x8C;
#endif
    s32 count = bondinvCountTotalItemsInInv();
    s32 i, h = 0, w = 0, widest = 0;
    f32 y0;

    if (get_debug_gunwatchpos_flag())
        return;
    if (py < (f32) base_y || py >= (f32) (base_y + lineheight * 5))
        return;
    for (i = 0; i < count; i++)
    {
        textMeasure(&h, &w, (char *) bondinvGetNameByIndex(i),
                    ptrFontBankGothicChars, ptrFontBankGothic, lineheight);
        if (w > widest) widest = w;
    }
    if (px < (f32) 0x4B - SL_PAD || px > (f32) (widest + 0x52) + SL_PAD)
        return;

    y0 = (f32) (base_y + watch_inventory_text_y);
    i = (s32) ((py - y0) / (f32) lineheight);
    if (py < y0 || i < 0 || i >= count)
        return;
    out->kind = HIT_ITEM;
    out->row = i;
}

/* ------------------------------------------------------------------------
 * Actions
 * --------------------------------------------------------------------- */

/* The zoom each page is entered with, from the watch_screenN_navigation
 * functions (options.c:756-926): status WATCHZOOM2, equipment WATCHZOOM1,
 * controls and options WATCHZOOM3, briefing WATCHZOOM1, SIGHTLINE WATCHZOOM3
 * (sl_watch_sightline_navigation and its neighbours, #44). */
static f32 page_zoom(s32 page)
{
    switch (page)
    {
    case WATCH_INDEX_MISSION_STATUS:   return WATCHZOOM2;
    case WATCH_INDEX_INVENTORY:        return WATCHZOOM1;
    case WATCH_INDEX_CONTROL_OPTIONS:  return WATCHZOOM3;
    case WATCH_INDEX_GAME_OPTIONS:     return WATCHZOOM3;
    case WATCH_INDEX_SL_SIGHTLINE:     return WATCHZOOM3;
    default:                           return WATCHZOOM1;
    }
}

/* Go to a page the way its neighbours' navigation does: the row index of the
 * destination reset, the stick latch cleared, and the beep + zoom only when
 * the zoom actually changes (controls <-> options share one and are silent,
 * options.c:872-875 / :887-890). Any select latch is dropped - the pad
 * cannot change page while latched, the mouse simply un-latches. */
static void go_to_page(s32 page)
{
    s32 from = (s32) watch_screen_index;
    if (page == from)
        return;
    reset_watch_item_is_actively_selected();
    zero_D_800409A4();
    sl_reset_sightline_row_index();     /* leaving SIGHTLINE closes its children (#46 / #45) */
    switch (page)
    {
    case WATCH_INDEX_CONTROL_OPTIONS: reset_controller_options_index(); break;
    case WATCH_INDEX_GAME_OPTIONS:    reset_game_options_index();       break;
    case WATCH_INDEX_SL_SIGHTLINE:    sl_reset_sightline_row_index();   break;
    default: break;
    }
    watch_screen_index = (u32) page;
    set_controlstick_lr_disabled();
    if (page_zoom(page) != page_zoom(from))
    {
        sub_GAME_7F0A5210();
        trigger_watch_zoom(page_zoom(page), 15.0f);
    }
    s_equip_pending = 0;
    s_drag_track = -1;
    s_drag_slider = -1;
}

/* Highlight a Game Options row as the stick path does (sub_GAME_7F0A5998):
 * the latch is dropped when the row changes. */
static void game_options_highlight(s32 row)
{
    if ((s32) game_options_index == row)
        return;
    game_options_index = (u32) row;
    disable_watch_stick_y_nav_ready();
    reset_watch_item_is_actively_selected();
}

/* Set a toggle row's value with the same write and sound the latched
 * left/right path makes (game_option_toggle_input -> game_option_select_value). */
static void game_options_set_value(s32 row, s32 value)
{
    if (row >= GAME_OPTIONS_INDEX_LOOK_UPDOWN && row <= GAME_OPTIONS_INDEX_RATIO)
    {
        struct game_options *e = &game_options_entries[row - GAME_OPTIONS_INDEX_LOOK_UPDOWN];
        if (value == 2 && e->text[3] == 0)
            return;
        if ((s32) e->current_value != value)
            game_option_select_value(&e->current_value, (u32) value);
    }
}

/* SIGHTLINE (#44): highlight a row as sl_watch_sightline_navigation does
 * (the latch dropped on a row change), and set a row's value with the same
 * confirm sound and stick latch the latched left/right path uses
 * (sl_sightline_row_input: game_option_select_value on a local, then the
 * one setter), reading and writing the same state options.c reads. */
static void sightline_highlight(s32 row)
{
    if ((s32) sl_sightline_row_index == row)
        return;
    sl_sightline_row_index = (u32) row;
    disable_watch_stick_y_nav_ready();
    reset_watch_item_is_actively_selected();
}

/* A cell chosen on a row of the view in force: the one press helper the
 * latched LEFT / RIGHT path uses (options.c sl_sightline_click: a toggle's
 * off / on with the confirm sound and stick latch, a value row's step). */
static void sightline_set_value(s32 row, s32 value)
{
    sl_sightline_click(row, value);
}

/* Set a volume track from a 0..1 fraction, through the same setters the
 * slider draw applies each frame (set_mTrack2Vol / sndApplyVolumeAllSfxSlot). */
static void track_set(s32 track, f32 t)
{
    s32 v = (s32) (t * (f32) VOLUME_MAX + 0.5f);
    if (v < 0) v = 0;
    if (v > VOLUME_MAX) v = VOLUME_MAX;
    if (track == 0)
        set_mTrack2Vol((u16) v);
    else
        sub_GAME_7F0A91A0((u16) v);
}

/* Abort the mission: the lines watch_screen0_navigation runs on A/Z with
 * confirm highlighted (options.c:767-774). */
static void abort_mission(void)
{
    D_800409A4 = 0;
    set_missionstate(MISSION_STATE_0);
    bossRunTitleStage();
    mission_failed_or_aborted = TRUE;
    deleteCurrentSelectedFolder();
}

static void apply_hover(const struct hit *h)
{
    switch (watch_screen_index)
    {
    case WATCH_INDEX_GAME_OPTIONS:
        if (h->kind == HIT_LABEL || h->kind == HIT_VALUE || h->kind == HIT_NONE)
        {
            if (h->row >= 0) game_options_highlight(h->row);
        }
        else if (h->kind == HIT_TRACK)
            game_options_highlight(h->row);
        break;

    case WATCH_INDEX_SL_SIGHTLINE:
        if ((h->kind == HIT_LABEL || h->kind == HIT_VALUE || h->kind == HIT_NONE || h->kind == HIT_SLIDER) && h->row >= 0)
        {
            if (sl_watch_bindings_is_open())
                sl_watch_bindings_hover(h->row);     /* the child's list (#46) */
            else
                sightline_highlight(h->row);
        }
        break;

    case WATCH_INDEX_CONTROL_OPTIONS:
        /* Rows move only while nothing is latched - the pad's rule
         * (controller_options_controlstyle_navigation). */
        if (!watch_item_is_actively_selected && h->row >= 0
            && (h->kind == HIT_LABEL || h->kind == HIT_STYLE || h->kind == HIT_NONE))
        {
            if ((s32) controller_options_index != h->row)
            {
                controller_options_index = (u32) h->row;
                disable_watch_stick_y_nav_ready();
            }
        }
        break;

    case WATCH_INDEX_MISSION_STATUS:
        /* With abort: latched, the highlight follows the pointer between
         * confirm and cancel, as the stick moves it (draw_abort_cancel_confirm). */
        if (watch_item_is_actively_selected)
        {
            if (h->kind == HIT_CONFIRM) D_800409A4 = 1;
            else if (h->kind == HIT_CANCEL) D_800409A4 = 0;
        }
        break;

    default:
        /* Equipment: no hover - the list scrolls to keep the selection on
         * its centre line, so a hover that selected would scroll the row it
         * selected out from under the pointer. Briefing: nothing to hover. */
        break;
    }
}

static void apply_click(const struct hit *h)
{
    if (h->kind == HIT_PAGE)
    {
        go_to_page(h->row);
        return;
    }

    switch (watch_screen_index)
    {
    case WATCH_INDEX_GAME_OPTIONS:
        if (h->kind == HIT_LABEL)
        {
            /* A on this row: highlight and toggle the select latch. */
            if ((s32) game_options_index != h->row)
                game_options_highlight(h->row);
            watch_play_beep_sound();
        }
        else if (h->kind == HIT_VALUE)
        {
            game_options_highlight(h->row);
            game_options_set_value(h->row, h->value);
        }
        else if (h->kind == HIT_TRACK)
        {
            game_options_highlight(h->row);
            track_set(h->row, h->t);
            /* A drag only while the button is STILL down: a press and
             * release inside one frame (a probed click; a fast real one)
             * has already released here, and a drag armed after its
             * release would move this track to the NEXT press's x before
             * that press applied (measured 2026-09-19 on the #50 bars). */
            s_drag_track = s_lmb_down ? h->row : -1;
        }
        break;

    case WATCH_INDEX_SL_SIGHTLINE:
        if (sl_watch_bindings_is_open())
        {
            /* The child's rows (#46): a label is A on that row, a value is
             * chosen directly - the same two rules as the page's own rows. */
            if (h->kind == HIT_LABEL)
                sl_watch_bindings_click(h->row, -1);
            else if (h->kind == HIT_VALUE)
                sl_watch_bindings_click(h->row, h->value);
        }
        else if (h->kind == HIT_LABEL)
        {
            if ((s32) sl_sightline_row_index != h->row)
                sightline_highlight(h->row);
            sl_sightline_click(h->row, -1);         /* a sub-menu opens, BACK returns, BINDINGS opens (#45 / #46) */
        }
        else if (h->kind == HIT_VALUE)
        {
            sightline_highlight(h->row);
            sightline_set_value(h->row, h->value);
        }
        else if (h->kind == HIT_SLIDER)
        {
            /* The track rule (#50): the value at the click, and the bar
             * follows the pointer while the button stays down. */
            sightline_highlight(h->row);
            sl_sightline_slide(h->row, h->t);
            s_drag_slider = s_lmb_down ? h->row : -1;   /* the track's rule above */
        }
        break;

    case WATCH_INDEX_CONTROL_OPTIONS:
        if (h->kind == HIT_LABEL)
        {
            if (!watch_item_is_actively_selected && (s32) controller_options_index != h->row)
            {
                controller_options_index = (u32) h->row;
                disable_watch_stick_y_nav_ready();
            }
            if ((s32) controller_options_index == h->row)
                watch_play_beep_sound();
        }
        else if (h->kind == HIT_STYLE)
        {
            /* The value beside a row (#63): the next layout - a BUTTON
             * LAYOUT preset re-seeds the pad slots, a STICK LAYOUT is live
             * on the next poll - with the confirm sound and the stick latch
             * of a step. Rows move only while nothing is latched. */
            u32 dummy = 0;
            if (!watch_item_is_actively_selected && (s32) controller_options_index != h->row)
            {
                controller_options_index = (u32) h->row;
                disable_watch_stick_y_nav_ready();
            }
            game_option_select_value(&dummy, 1);
            sl_watch_layout_step(h->row, 1);
        }
        break;

    case WATCH_INDEX_MISSION_STATUS:
        if (h->kind == HIT_ABORT)
        {
            watch_play_beep_sound();
            D_800409A4 = 0;
        }
        else if (watch_item_is_actively_selected && h->kind == HIT_CONFIRM)
        {
            abort_mission();
        }
        else if (watch_item_is_actively_selected && h->kind == HIT_CANCEL)
        {
            reset_watch_item_is_actively_selected();
            D_800409A4 = 0;
        }
        break;

    case WATCH_INDEX_INVENTORY:
        if (h->kind == HIT_ITEM && !watch_item_is_actively_selected)
        {
            /* Select the row (the navigation derives g_curWatchItemIndex from
             * the integer part and settles the cursor on .5) and ask
             * sub_GAME_7F0A8378 to equip it once the list has settled. */
            watch_inventory_cursor_pos = (f32) h->row + 0.5f;
            g_curWatchItemIndex = h->row;
            s_equip_pending = h->row + 1;
        }
        break;

    default:
        break;
    }
}

static void debug_line(const char *tag, const struct hit *h, f32 px, f32 py)
{
    fprintf(stderr, "sightline watch: %s ptr=(%.0f,%.0f) page=%u hit=%s row=%d value=%d t=%.2f"
                    " | gopt=%u copt=%u slrow=%u latch=%d abort=%d inv=%d music=%u fx=%u style=%d minv=%d sprint=%d"
                    " fov=%d msens=%d ssens=%d smode=%d cmode=%d vals=%u%u%u%u%u%u%u%u\n",
            tag, px, py, watch_screen_index, kind_name(h->kind), h->row, h->value, h->t,
            game_options_index, controller_options_index, sl_sightline_row_index,
            watch_item_is_actively_selected, D_800409A4, g_curWatchItemIndex,
            (u32) get_mTrack2Vol(), (u32) call_sndGetSfxSlotFirstNaturalVolume(),
            cur_player_get_control_type(), sl_mouse_invert_y_get(), sl_sprint_enabled(),
            sl_fov_h16_displayed(), sl_mouse_sens_get(0), sl_mouse_sens_get(1),
            sl_action_mode(1), sl_action_mode(0),
            game_options_entries[0].current_value, game_options_entries[1].current_value,
            game_options_entries[2].current_value, game_options_entries[3].current_value,
            game_options_entries[4].current_value, game_options_entries[5].current_value,
            game_options_entries[6].current_value, game_options_entries[7].current_value);
}

/* ------------------------------------------------------------------------
 * The frame hook (draw_watch_current_page, interactive branch)
 * --------------------------------------------------------------------- */
void sl_watch_pointer_frame(Mtx *pagemtx)
{
    f32 u, v, px, py;
    unsigned int motion;
    int moved;
    struct hit h;

    /* Only the live, interactive watch (bondview2.c:3713: navigation runs in
     * state 5 alone); the closing animation still draws through this branch
     * and must not act. */
    if (g_CurrentPlayer == NULL
        || g_CurrentPlayer->watch_animation_state != WATCH_ANIMATION_0x5)
    {
        s_clicks = 0;
        s_drag_track = -1;
        s_drag_slider = -1;
        s_have_motion = 0;
        /* The watch is closing (or not yet interactive): the BINDINGS child
         * does not survive it - the next open starts on the ring (#46);
         * nor does a #45 sub-menu. */
        sl_reset_sightline_row_index();
        return;
    }
    if (watch_screen_index != WATCH_INDEX_INVENTORY)
        s_equip_pending = 0;

    /* The page witness (#44): under SL_INPUT_DEBUG, one line per page change
     * however it was made (pad, keyboard stick, page-bar click), with the
     * two SIGHTLINE settings beside it so a log shows what the page read. */
    if (debug_on())
    {
        static u32 last_page = 0xFFFFFFFFu;
        if (watch_screen_index != last_page)
        {
            fprintf(stderr, "sightline watch: page %d -> %u (slrow=%u minv=%d sprint=%d aspect=%d(%s) fov=%d)\n",
                    last_page == 0xFFFFFFFFu ? -1 : (int) last_page, watch_screen_index,
                    sl_sightline_row_index, sl_mouse_invert_y_get(), sl_sprint_enabled(),
                    sl_aspect_ratio(), sl_aspect_name(sl_aspect_ratio()), sl_fov_h16_displayed());
            last_page = watch_screen_index;
        }
    }

    if (!sl_menu_pointer_uv(&u, &v, &motion))
    {
        /* No pointer over the image: a click there is a click on nothing. */
        if (debug_on() && (s_clicks > 0 || s_have_motion))
            fprintf(stderr, "sightline watch: no pointer over the image (%d click(s) dropped)\n", s_clicks);
        s_clicks = 0;
        s_have_motion = 0;
        return;
    }
    px = u * (f32) viGetX();
    py = v * (f32) viGetY();

    if (!s_have_motion) { s_have_motion = 1; moved = 0; }
    else                moved = (motion != s_last_motion);
    s_last_motion = motion;

    h.kind = HIT_NONE; h.row = -1; h.value = 0; h.t = 0.0f;
    if (!hit_page_bar(pagemtx, px, py, &h)
        && check_watch_page_transistion_running() != 1)
    {
        switch (watch_screen_index)
        {
        case WATCH_INDEX_GAME_OPTIONS:    hit_game_options(pagemtx, px, py, &h); break;
        case WATCH_INDEX_CONTROL_OPTIONS: hit_control_options(px, py, &h);      break;
        case WATCH_INDEX_MISSION_STATUS:  hit_mission_status(px, py, &h);       break;
        case WATCH_INDEX_INVENTORY:       hit_equipment(px, py, &h);            break;
        case WATCH_INDEX_SL_SIGHTLINE:
            if (sl_watch_bindings_is_open()) hit_bindings(px, py, &h);
            else                            hit_sightline(pagemtx, px, py, &h);
            break;
        default: break;                   /* briefing: the bar only */
        }
    }

    /* A held button on a track follows the pointer, whatever else is under
     * it now - the drag was started on the track and ends with the button. */
    if (s_lmb_down && s_drag_track >= 0 && watch_screen_index == WATCH_INDEX_GAME_OPTIONS)
    {
        f32 l, t, r, b;
        if (watch_project_rect(pagemtx, (f32) SL_TRACK_X0,
                               (f32) (s_drag_track == 0 ? SL_TRACK_Z_MUSIC : SL_TRACK_Z_FX),
                               (f32) SL_TRACK_W, (f32) SL_TRACK_H, &l, &t, &r, &b) && r > l)
        {
            f32 tt = (px - l) / (r - l);
            if (tt < 0.0f) tt = 0.0f;
            if (tt > 1.0f) tt = 1.0f;
            track_set(s_drag_track, tt);
        }
    }
    else if (s_lmb_down && s_drag_slider >= 0 && watch_screen_index == WATCH_INDEX_SL_SIGHTLINE
             && !sl_watch_bindings_is_open() && SL_WROW_IS_SLIDER(sl_sightline_row_kind(s_drag_slider)))
    {
        /* The same rule on a SIGHTLINE slider row's bar (#50); the row's kind
         * is re-checked because a view change ends what the row was. */
        f32 l, t, r, b;
        if (sightline_bar_rect(pagemtx, s_drag_slider, &l, &t, &r, &b))
        {
            f32 tt = (px - l) / (r - l);
            if (tt < 0.0f) tt = 0.0f;
            if (tt > 1.0f) tt = 1.0f;
            sl_sightline_slide(s_drag_slider, tt);
        }
    }
    else if (moved)
    {
        apply_hover(&h);
    }

    if (debug_on())
    {
        static int last_kind = -1, last_row = -2, last_value = -1;
        if (s_clicks > 0 || h.kind != last_kind || h.row != last_row || h.value != last_value)
        {
            last_kind = h.kind; last_row = h.row; last_value = h.value;
            debug_line(s_clicks > 0 ? "CLICK" : "hover", &h, px, py);
        }
    }

    if (s_clicks > 0)
    {
        s_clicks = 0;               /* one click per frame; extras are the same spot */
        apply_click(&h);
        if (debug_on())
            debug_line("after", &h, px, py);
    }
}

/* ------------------------------------------------------------------------
 * The cursor (draw_watch_current_page, after the page has drawn)
 * --------------------------------------------------------------------- */

/* The game's crosshair at half size over the pointer, exactly as
 * gunDrawSight draws the sight (gunfire.c:6337-6350): the same image, the
 * same texSelect / display_image_at_position path, the same 16:9 x-squeeze,
 * only the half-extent halved. Drawn only while the pointer is CAPTURED (the
 * host cursor is the pointer otherwise; a probed pointer counts, for the
 * witness) and only on the interactive watch, and after the page so it sits
 * on top of the text, as the sight sits on top of the HUD. Framebuffer
 * coordinates, so it follows the window like every other texrect.
 *
 * ACROSS THE CONTENT RECT (owner-observed 2026-09-20: "in the watch the
 * cursor goes BEHIND watch UI/bezel at the edges and becomes hard/impossible
 * to see"). Measured before this change on a 1280x720 16:9 window: the
 * pointer in either 160 px band drew NOTHING - the mapping above answered
 * "not over the image" for a fraction outside [0,1] and this returned - while
 * the watch's 3D bezel, widened with the view (#45), is drawn right there. So
 * the crosshair vanished under a bezel that stayed; nothing was ever drawn
 * over it (draw order, depth and scissor were not the cause: the 2D layer
 * draws last with the depth test off, and inside the image the crosshair was
 * always visible). And a texrect could not have reached the band anyway: its
 * corners are unsigned, and draw_textured_rectangle clips xl < 0 to 0
 * (bondwalk2.c:43). Now the position is the EXTENDED one - the same u * 320
 * inside the image, continued into the bands and confined to the content
 * rect - carried to the renderer by its placement tag (src/gfx/sl_gfx_dl.c
 * `C0 'SLC'`: the rect's top-left, signed 1/4 px), while the texrect itself
 * is encoded at the nearest position whose corners are non-negative, so the
 * game's own clip never fires and the renderer moves the whole quad. Inside
 * the image the tag names the corner the untagged draw had - the 4:3
 * pixels are the ones they were. The hit tests (sl_watch_pointer_frame) keep
 * the safe-rect mapping: a click in a band is a click on nothing, as before. */
Gfx *sl_watch_pointer_draw(Gfx *gdl)
{
    int px, py, ww, wh;
    int safe[4], content[4];
    unsigned int motion;
    float lx, ly;
    f32 xypos[2], halfedxy[2];
    s32 xl4, yl4;
    int where;

    if (g_CurrentPlayer == NULL
        || g_CurrentPlayer->watch_animation_state != WATCH_ANIMATION_0x5)
        return gdl;
    if (!(sl_input_pointer_captured() || sl_input_pointer_probed()) || crosshairimage == NULL)
        return gdl;
    if (!sl_input_pointer_get(&px, &py, &ww, &wh, &motion))
        return gdl;
    if (!sl_gfx_present_rect(wh, &safe[0], &safe[1], &safe[2], &safe[3]))
        return gdl;
    if (!sl_gfx_content_rect(wh, &content[0], &content[1], &content[2], &content[3]))
        return gdl;
    where = sl_display_pointer_logical(safe, content, px, py,
                                       (float) viGetX(), (float) viGetY(), &lx, &ly);
    if (where < 0)
        return gdl;

    halfedxy[0] = 16.0f * SL_WATCH_CURSOR_SCALE;
    halfedxy[1] = 16.0f * SL_WATCH_CURSOR_SCALE;
    if (get_screen_ratio() == SCREEN_RATIO_16_9)
        halfedxy[0] = halfedxy[0] * 0.75f;

    /* Where the crosshair IS drawn: the extended position's corner, as
     * draw_textured_rectangle would compute it from an unclipped centre. */
    xl4 = (s32) ((lx - halfedxy[0]) * 4.0f);
    yl4 = (s32) ((ly - halfedxy[1]) * 4.0f);

    /* What the texrect ENCODES: the same centre, kept where both corners
     * are non-negative and inside the framebuffer so the game's own clip
     * (xl < 0 -> 0 with an s shift) cannot alter the quad the tag moves. */
    xypos[0] = lx;
    xypos[1] = ly;
    if (xypos[0] < halfedxy[0])                      xypos[0] = halfedxy[0];
    if (xypos[0] > (f32) viGetX() - halfedxy[0])     xypos[0] = (f32) viGetX() - halfedxy[0];
    if (xypos[1] < halfedxy[1])                      xypos[1] = halfedxy[1];
    if (xypos[1] > (f32) viGetY() - halfedxy[1])     xypos[1] = (f32) viGetY() - halfedxy[1];

    if (debug_on())
    {
        static s32 last_x4 = 0x7FFFFFFF, last_y4 = 0x7FFFFFFF, last_where = -2;
        if (xl4 != last_x4 || yl4 != last_y4 || where != last_where)
        {
            last_x4 = xl4; last_y4 = yl4; last_where = where;
            fprintf(stderr, "sightline watch: cursor drawn at (%.1f,%.1f) %s window=(%d,%d)\n",
                    lx, ly, where == 2 ? "safe" : where == 1 ? "BAND" : "edge", px, py);
        }
    }

    gdl->words.w0 = (0xC0u << 24) | 0x00534C43u;
    gdl->words.w1 = (((u32) xl4 & 0xFFFFu) << 16) | ((u32) yl4 & 0xFFFFu);
    gdl++;

    texSelect(&gdl, crosshairimage, 4, 0, 0);
    display_image_at_position(&gdl, xypos, halfedxy, 0x20, 0x20, 0, 0, 1,
                              0xFF, 0xFF, 0xFF, SL_WATCH_CURSOR_ALPHA,
                              (crosshairimage->level > 0), 0);
    return gdl;
}

#endif /* !__sgi */
