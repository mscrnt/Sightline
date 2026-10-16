/**
 * sl_brief_plan.c - Egyptian's Q Branch attachment: a recovered floor plan.
 *
 * On the pre-mission briefing of el-Saghira (LEVELID_EGYPT), Q Branch page
 * only, one discreet line under Q's text - "Attachment: Recovered floor
 * plan" - opens an annotated sketch of the Golden Gun's chamber with the safe
 * route across its trapped floor. Closed by default, closed again whenever
 * the page or the briefing is left, so the solution is never on screen
 * unless asked for. Every other briefing, and this one's other pages, draw
 * exactly as before.
 *
 * INPUT. The line is hit-tested by the one front-end cursor (stick, keys and
 * mouse move it alike) against the rectangle the constructor last drew it
 * in - the same box, so what lights is what takes the press. On it, A or Z
 * opens the sheet; while the sheet is open A, Z or B closes it and nothing
 * else. Either way the tabs are not highlighted, so the briefing's own A/Z
 * arm (front.c interface_menu0A_briefing) finds no tab to act on and only
 * plays its click, and its B arm is skipped for the press that closed the
 * sheet. START launches as always, from any state.
 *
 * THE PLAN. The grid, the doorway, the pedestal and the bays are derived from
 * the stage's data; the route's sequence of stones follows the supplied
 * sketch and is checked statically against the game's trap pads (below) -
 * a check of clearance, not a replay of the mission:
 *   - room 19, the Golden Gun's chamber: x 1011..3222, z 2117..3038 world
 *     (sl_map bounds), floor 367; a 5 x 12 grid of 184-unit stones (the room
 *     over 5 and 12; the trap pads stand at that pitch, setup units x 3.905,
 *     prop.c's room scale);
 *   - rows from the doorway: row 11 is the west end, where room 18's
 *     corridor (z 2488..2671) opens on column 2; column 0 is the low-z side,
 *     the player's left on entering (measured: on column 0 facing east the
 *     low-z wall fills the left of the view);
 *   - the pedestal: the Golden Gun's case at world 2575, 2583 - column 2,
 *     row 3;
 *   - the four sealed bays: the stone blocks whose ten covers hide the drone
 *     guns, the guns at 2018.9 / 3131.8 by 2214.2 / 2952.2 - columns 0 and 4
 *     of rows 0 and 6;
 *   - the route: tile centres (2,11) .. (2,4), then the pedestal. UsetupcrypZ
 *     ai_17 springs the trap when Bond comes within 9 decimetres (90 units,
 *     chrai.c: ntohs(DISTANCE) * 10.0f) of pads 0x97..0xae other than 0xa6,
 *     and opens the case within 10 (100) of 0xa6. Every stone of the route
 *     is about 165 units (165.07 at the nearest) from every trap pad, every
 *     straight step between two of them at least 164.8 (164.875 at the
 *     nearest), and 0xa6 lies 23.25 units from the centre of (2,4). Of the
 *     six ways to lay the 5 x 12 grid with row 11 at the west
 *     doorway - mirrored or not, or a row off either way - only the one on
 *     the room's bounds keeps the route clear; each other puts a stone of it
 *     within 7 units of a trap pad.
 *
 * Drawn with the front end's own primitives: frontPrintText in the briefing's
 * font, and the menus' filled box (microcode_constructor_related_to_menus)
 * for the sheet, the grid, the bays, the pedestal and the route. No image.
 *
 * Everything is !__sgi; front.c carries the three calls, the interface's B-arm
 * guard and its one local, each under #ifndef __sgi (D-056).
 */
#ifndef __sgi

#include <stdio.h>
#include <ultra64.h>
#include <bondgame.h>
#include <fr.h>
#include <snd.h>
#include "joy.h"
#include "front.h"
#include "textrelated.h"

extern Gfx  *frontPrintText(Gfx *gdl, s32 *x, s32 *y, s8 *text, s32 second_font_table, s32 first_font_table, s32 arg6, s32 view_x, s32 view_y, s32 arg9, s32 arga);
/* The N64 include tree's <stdlib.h> has no getenv (sl_cheat.c's rule). */
extern char *getenv(const char *);

#define FONT_CHARS ((s32) ptrFontZurichBoldChars)
#define FONT       ((s32) ptrFontZurichBold)
#define CONFIRM    (Z_TRIG | A_BUTTON)

enum { SL_BRIEF_PLAN_NONE, SL_BRIEF_PLAN_HOVER, SL_BRIEF_PLAN_OPEN };

/* The sheet laid over the page body, below the page title (0x8F) and clear
 * of the tabs (TABS_LEFT_EDGE 390). */
#define SHEET_X0   52
#define SHEET_Y0   156
#define SHEET_X1   378
#define SHEET_Y1   325
/* The plan: 5 x 12 stones of TILE units, row 0 at the top. */
#define TILE       11
#define PLAN_COLS  5
#define PLAN_ROWS  12
#define PLAN_X     72
#define PLAN_Y     176
#define LEGEND_X   150

/* Ink. RGBA, as the menus' box takes it. */
#define INK_SHEET    0xEEE8D2FF          /* the attached sheet */
#define INK_EDGE     0x8C8268FF          /* its edge */
#define INK_FLOOR    0xDDD3B0FF          /* the chamber's floor wash */
#define INK_GRID     0x7A6E54C8          /* the joints between stones */
#define INK_WALL     0x3C342AFF          /* the chamber's walls */
#define INK_BAY      0x6E5478E6          /* a sealed bay */
#define INK_PEDESTAL 0x141414FF          /* the pedestal */
#define INK_SAFE     0xE6A08C90          /* a stone of the route */
#define INK_ROUTE    0x8C1414FF          /* the route */
#define INK_CLIP     0x6E6E6EFF          /* the clip */

static const s8 s_route[][2] = {          /* (column, row), entrance first */
    {2, 11}, {1, 11}, {0, 11}, {0, 10}, {0, 9}, {1, 9}, {2, 9}, {3, 9},
    {3, 8},  {3, 7},  {2, 7},  {2, 6},  {1, 6}, {1, 5}, {1, 4}, {2, 4},
};
#define ROUTE_N ((s32) (sizeof s_route / sizeof s_route[0]))
static const s8 s_bays[][2] = { {0, 0}, {4, 0}, {0, 6}, {4, 6} };
#define PEDESTAL_COL 2
#define PEDESTAL_ROW 3
#define ENTRANCE_COL 2

static s32 s_open;                       /* the sheet is open */
static s32 s_hover;                      /* the cursor is on the line */
static s32 s_have_rect;                  /* the line was drawn last frame */
static s32 s_rx0, s_ry0, s_rx1, s_ry1;   /* ... in this box */
static s32 s_witness = -1;

static s32 plan_here(s32 stage_id, s32 page)
{
    return stage_id == LEVELID_EGYPT && page == BRIEFING_Q;
}

static void plan_witness(void)
{
    s32 now = s_open ? 2 : s_hover ? 1 : 0;

    if (now == s_witness)
        return;
    s_witness = now;
    if (getenv("SL_INPUT_DEBUG") != NULL)
        fprintf(stderr, "sightline front: brief-plan %s cursor=(%.1f,%.1f)\n",
                now == 2 ? "open" : now == 1 ? "hover" : "closed", cursor_h_pos, cursor_v_pos);
}

/* init_menu0A_briefing: every briefing starts with the sheet closed. */
void sl_brief_plan_reset(void)
{
    s_open = 0;
    s_hover = 0;
    s_have_rect = 0;
    plan_witness();
}

/* interface_menu0A_briefing, after the tab highlights and before its button
 * arms: takes this frame's A/Z when the line is under the cursor or the sheet
 * is open, and B when the sheet is open. Returns SL_BRIEF_PLAN_OPEN when the
 * sheet was open at the start of the frame (the caller then skips its B arm),
 * SL_BRIEF_PLAN_HOVER when the cursor is on the line (the caller clears the
 * tab highlights for both), else SL_BRIEF_PLAN_NONE. */
s32 sl_brief_plan_interface(s32 stage_id, s32 page)
{
    s32 was_open;

    if (!plan_here(stage_id, page))
    {
        s_open = 0;
        s_hover = 0;
        s_have_rect = 0;
        plan_witness();
        return SL_BRIEF_PLAN_NONE;
    }
    was_open = s_open;
    s_hover = !was_open && s_have_rect && cursor_h_pos < TABS_LEFT_EDGE
           && cursor_h_pos >= (f32) s_rx0 && cursor_h_pos < (f32) s_rx1
           && cursor_v_pos >= (f32) s_ry0 && cursor_v_pos < (f32) s_ry1;
    if (joyGetButtonsPressedThisFrame(PLAYER_1, START_BUTTON))
    {
        s_open = 0;                      /* the mission launches */
    }
    else if (joyGetButtonsPressedThisFrame(PLAYER_1, CONFIRM))
    {
        if (was_open)
            s_open = 0;
        else if (s_hover)
            s_open = 1;
    }
    else if (was_open && joyGetButtonsPressedThisFrame(PLAYER_1, B_BUTTON))
    {
        s_open = 0;
    }
    plan_witness();
    return was_open ? SL_BRIEF_PLAN_OPEN : s_hover ? SL_BRIEF_PLAN_HOVER : SL_BRIEF_PLAN_NONE;
}

static Gfx *box(Gfx *DL, s32 x0, s32 y0, s32 x1, s32 y1, u32 rgba)
{
    return microcode_constructor_related_to_menus(DL, x0, y0, x1, y1, rgba);
}

static Gfx *text(Gfx *DL, s32 x, s32 y, char *s, s32 colour)
{
    return frontPrintText(DL, &x, &y, (s8 *) s, FONT_CHARS, FONT, colour, viGetX(), viGetY(), 0, 0);
}

static s32 tile_cx(s32 col) { return PLAN_X + col * TILE + TILE / 2; }
static s32 tile_cy(s32 row) { return PLAN_Y + row * TILE + TILE / 2; }

static Gfx *plan_draw_sheet(Gfx *DL)
{
    static char l_title[]   = "RECOVERED FLOOR PLAN\n";
    static char l_survey[]  = "Excavation survey, inner chamber.\n";
    static char l_route[]   = "Safe stones, in order\n";
    static char l_pedestal[]= "Pedestal\n";
    static char l_bay[]     = "Sealed bay\n";
    static char l_keep[]    = "Keep to the line.\n";
    static char l_entrance[]= "ENTRANCE\n";
    s32 i, x0, y0, x1, y1;

    /* the sheet, its edge and its clip */
    DL = box(DL, SHEET_X0, SHEET_Y0, SHEET_X1, SHEET_Y1, INK_SHEET);
    DL = box(DL, SHEET_X0, SHEET_Y0, SHEET_X1, SHEET_Y0 + 1, INK_EDGE);
    DL = box(DL, SHEET_X0, SHEET_Y1 - 1, SHEET_X1, SHEET_Y1, INK_EDGE);
    DL = box(DL, SHEET_X0, SHEET_Y0, SHEET_X0 + 1, SHEET_Y1, INK_EDGE);
    DL = box(DL, SHEET_X1 - 1, SHEET_Y0, SHEET_X1, SHEET_Y1, INK_EDGE);
    DL = box(DL, SHEET_X1 - 40, SHEET_Y0 - 5, SHEET_X1 - 36, SHEET_Y0 + 9, INK_CLIP);
    DL = text(DL, SHEET_X0 + 20, SHEET_Y0 + 4, l_title, 0xFF);

    /* the floor, the stones of the route, the joints */
    x0 = PLAN_X;
    y0 = PLAN_Y;
    x1 = PLAN_X + PLAN_COLS * TILE;
    y1 = PLAN_Y + PLAN_ROWS * TILE;
    DL = box(DL, x0, y0, x1, y1, INK_FLOOR);
    for (i = 0; i < ROUTE_N; i++)
    {
        s32 tx = PLAN_X + s_route[i][0] * TILE, ty = PLAN_Y + s_route[i][1] * TILE;
        DL = box(DL, tx + 1, ty + 1, tx + TILE, ty + TILE, INK_SAFE);
    }
    for (i = 1; i < PLAN_COLS; i++)
        DL = box(DL, x0 + i * TILE, y0, x0 + i * TILE + 1, y1, INK_GRID);
    for (i = 1; i < PLAN_ROWS; i++)
        DL = box(DL, x0, y0 + i * TILE, x1, y0 + i * TILE + 1, INK_GRID);

    /* the bays and the pedestal */
    for (i = 0; i < (s32) (sizeof s_bays / sizeof s_bays[0]); i++)
    {
        s32 tx = PLAN_X + s_bays[i][0] * TILE, ty = PLAN_Y + s_bays[i][1] * TILE;
        DL = box(DL, tx, ty, tx + TILE + 1, ty + TILE + 1, INK_BAY);
    }
    DL = box(DL, PLAN_X + PEDESTAL_COL * TILE + 2, PLAN_Y + PEDESTAL_ROW * TILE + 2,
             PLAN_X + (PEDESTAL_COL + 1) * TILE - 1, PLAN_Y + (PEDESTAL_ROW + 1) * TILE - 1, INK_PEDESTAL);

    /* the walls, open at the doorway */
    DL = box(DL, x0 - 2, y0 - 2, x1 + 2, y0, INK_WALL);
    DL = box(DL, x0 - 2, y0, x0, y1 + 2, INK_WALL);
    DL = box(DL, x1, y0, x1 + 2, y1 + 2, INK_WALL);
    DL = box(DL, x0 - 2, y1, x0 + ENTRANCE_COL * TILE, y1 + 2, INK_WALL);
    DL = box(DL, x0 + (ENTRANCE_COL + 1) * TILE + 1, y1, x1 + 2, y1 + 2, INK_WALL);

    /* the route: in at the doorway, stone to stone, to the pedestal's face */
    {
        s32 px = tile_cx(ENTRANCE_COL), py = y1 + 5;
        for (i = 0; i <= ROUTE_N; i++)
        {
            s32 nx, ny;
            if (i < ROUTE_N)
            {
                nx = tile_cx(s_route[i][0]);
                ny = tile_cy(s_route[i][1]);
            }
            else
            {
                nx = tile_cx(PEDESTAL_COL);
                ny = PLAN_Y + (PEDESTAL_ROW + 1) * TILE - 1;
            }
            if (nx == px)
                DL = box(DL, px - 1, (ny < py ? ny : py) - 1, px + 1, (ny < py ? py : ny) + 1, INK_ROUTE);
            else
                DL = box(DL, (nx < px ? nx : px) - 1, py - 1, (nx < px ? px : nx) + 1, py + 1, INK_ROUTE);
            if (i < ROUTE_N)
                DL = box(DL, nx - 2, ny - 2, nx + 2, ny + 2, INK_ROUTE);
            px = nx;
            py = ny;
        }
        /* the arrow at the doorway, pointing in, and the arrowhead at the
         * pedestal's face, pointing on: the route's two ends */
        px = tile_cx(ENTRANCE_COL);
        for (i = 0; i < 4; i++)
            DL = box(DL, px - 1 - i, y1 + 5 + i * 2, px + 1 + i, y1 + 7 + i * 2, INK_ROUTE);
        px = tile_cx(PEDESTAL_COL);
        py = PLAN_Y + (PEDESTAL_ROW + 1) * TILE;
        for (i = 0; i < 3; i++)
            DL = box(DL, px - 1 - i, py + i * 2, px + 1 + i, py + 2 + i * 2, INK_ROUTE);
        DL = text(DL, PLAN_X + PLAN_COLS * TILE + 8, y1 + 4, l_entrance, 0xFF);
    }

    /* the legend */
    DL = text(DL, LEGEND_X, PLAN_Y + 2, l_survey, 0xFF);
    DL = box(DL, LEGEND_X, PLAN_Y + 22, LEGEND_X + 9, PLAN_Y + 31, INK_SAFE);
    DL = box(DL, LEGEND_X + 3, PLAN_Y + 25, LEGEND_X + 7, PLAN_Y + 29, INK_ROUTE);
    DL = text(DL, LEGEND_X + 16, PLAN_Y + 21, l_route, 0xFF);
    DL = box(DL, LEGEND_X + 1, PLAN_Y + 40, LEGEND_X + 8, PLAN_Y + 47, INK_PEDESTAL);
    DL = text(DL, LEGEND_X + 16, PLAN_Y + 37, l_pedestal, 0xFF);
    DL = box(DL, LEGEND_X, PLAN_Y + 55, LEGEND_X + 9, PLAN_Y + 64, INK_BAY);
    DL = text(DL, LEGEND_X + 16, PLAN_Y + 53, l_bay, 0xFF);
    DL = text(DL, LEGEND_X, PLAN_Y + 80, l_keep, 0xFF);
    return DL;
}

/* constructor_menu0A_briefing, after the page text, before the tabs:
 * `text_y` is where frontPrintText left the text's pen. Draws the line, or the
 * sheet over the page body, on Egyptian's Q Branch page only. */
Gfx *sl_brief_plan_draw(Gfx *DL, s32 stage_id, s32 page, s32 text_y)
{
    static char l_line[] = "Attachment: Recovered floor plan\n";
    s32 x, y, w, h;

    if (!plan_here(stage_id, page))
    {
        s_have_rect = 0;
        return DL;
    }
    if (s_open)
        return plan_draw_sheet(DL);

    x = 0x37;
    y = text_y + 10;
    w = 0;
    h = 0;
    textMeasure(&h, &w, l_line, ptrFontZurichBoldChars, ptrFontZurichBold, 0);
    s_rx0 = x - 2;
    s_ry0 = y - 2;
    s_rx1 = x + w + 3;
    s_ry1 = y + 0xE;
    s_have_rect = 1;
    if (s_hover)
        DL = box(DL, s_rx0, s_ry0, s_rx1, s_ry1, 0x32);
    return text(DL, x, y, l_line, 0xFF);
}

#endif
