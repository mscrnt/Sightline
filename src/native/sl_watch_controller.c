/**
 * sl_watch_controller.c - the device-matched controller on the watch's Control
 * Options page (#63): with an Xbox- or PlayStation-family pad driving the
 * game and its model compiled, the page draws that controller in place of
 * the N64 pad, its parts moving with the player's hands, the family's own
 * button names and the actions bound to them around it - and, above it, the
 * page's two rows, BUTTON LAYOUT and STICK LAYOUT (2026-09-20: the N64
 * CONTROL STYLE row and the ORIGINAL / MODERN profile are gone; the pad is
 * always the modern controller).
 *
 * WHAT MOVES, AND FROM WHAT. The pose is the PHYSICAL state of the pad
 * (sl_input_pad_visual: SDL's own axes and buttons, addressed by canonical
 * part) - not the N64 buttons the game receives and not the actions the
 * registry raised. The picture therefore shows what the hands are doing
 * whatever the bindings say, and the right stick tilts while looking even
 * though no C button is ever raised for it. Per part:
 *
 *   LEFT_STICK / RIGHT_STICK   tilt about the pivot with the axis, the N64
 *                              stick's own sense (options.c draw_watch_
 *                              controller / gunfire.c watchRenderController:
 *                              -x about z, -y about x); 25 degrees at full
 *   FACE_*, MENU, BACK, GUIDE, depress along -y while held (the N64 buttons'
 *   MUTE, TOUCHPAD, DPAD_*     own y -= 10, at the two models' button height)
 *                              and take a highlight tint
 *   DPAD                       rocks 10 degrees toward the held direction, as
 *                              the N64 d-pad does (four directions, one part)
 *   LEFT/RIGHT_SHOULDER        rock 10 degrees about the pivot while held
 *   LEFT/RIGHT_TRIGGER         swing about the pivot with the analog pull
 *
 * THE LABELS (2026-09-20, the owner: "they are missing the button actions
 * along the side of it"). The original page prints, in two columns beside
 * the N64 pad (x 0x32 left-aligned, x 0x10e right-aligned, sub_GAME_7F0A9AB8),
 * the control style's action name per button - AIM, LOOK, FIRE, WEAPON... -
 * and lights a label white-outlined while its button is held. The modern
 * page does the same with the LIVE binding registry as the source of truth:
 * for every labelled physical control (LT RT LB RB, the four face buttons,
 * L3 R3, the d-pad, Start / Options, View / Create) the line is the
 * family's own name for the control (sl_bindings_source_label: A on an Xbox
 * pad, CROSS on a DualSense; the registry's persisted token never changes)
 * followed by the action(s) the registry holds for it
 * (sl_bindings_actions_for_source - a custom binding shows at once; a
 * control bound to nothing is omitted), or, for the two controls the
 * registry does not own, what they do: Start PAUSE (the watch), View / Create
 * MARK (the run mark, sl_input.c read_pad). Since round 6 (2026-09-20) the
 * d-pad's row is the union of its four directions' bindings (it used to
 * print the cartridge's Honey word LOOK, a fact about the N64 d-pad and not
 * a binding) and each stick's label is two rows - its analog role from the
 * STICK LAYOUT over "CLICK <action>" - see g_controls. Never an N64 name.
 *
 * WHERE THEY SIT: beside the part they name. Each labelled part's rest pivot
 * (the model's part table) is projected through the page's own modelview
 * (finalmtx, the matrix this draw converts) and perspective (guPerspective
 * 50.5 / 4:3 / 1000 / 3000, options.c:3708) into framebuffer pixels, so the
 * labels follow the model whatever the window size or aspect (#45's
 * viewport). A control on the pad's left half (pivot x < 0) goes to the left
 * column, the rest to the right; each column is ordered by the rest pivots
 * (top edge first - the triggers and bumpers - then down the face), which is
 * stable under the page's spin, and its rows are spread to a minimum pitch
 * top to bottom so nothing overlaps. No leader lines: the page's 2D pass is
 * text (draw_options_labels) and this stays inside it - a line primitive
 * would be a new drawing path, and the original page draws none either.
 *
 * THE ICONS (2026-09-20, round 5, #63 / #64): as on the original page, whose
 * icons ARE the pad's own button nodes drawn a second time beside the text
 * (gunfire.c watchRenderController, animatebuttons), every label carries the
 * model's own part drawn alone beside it - the A with its letter, the
 * CROSS with its symbol, the bumper, the trigger, the stick top, the
 * d-pad, Start / View - through the renderer's part-only bridge command
 * (sl_asset_override_emit_part), fitted to one icon height, tilted toward
 * the camera, lit and exposed like the pad and carrying the same live pose,
 * so a held control moves and lights on the pad and on its icon at once.
 * The text beside an icon is the action alone: "icon + word", the
 * original's look. See THE LABEL COLUMNS AND THE ICONS below.
 *
 * The frame is GjoypadZ's - the models are fitted to it (data/asset-
 * overrides/source/controllers/README) - so the page's own matrices, look-at
 * and perspective are handed over unchanged: the model sits where the N64
 * pad sits and spins with the same speed.
 *
 * FALLBACK IS THE ORIGINAL. No pad, a GENERIC family, or a model that is
 * absent or rejected, and sl_watch_controller_modern says 0: the page draws
 * GjoypadZ and its labels exactly as before (under the pinned Honey names).
 * Modern INPUT never depends on this file - the artwork is the last thing in
 * the chain, not the first.
 *
 * SL_PAD_DBG=1 (developer seam, off by default): one line per change of the
 * page's decision (family, model id, availability, which arm), one line per
 * 60 emits with the fade and the pad's physical snapshot, one per 60 draws
 * with the label placement (each label's text, column and framebuffer y),
 * and on the renderer's side (sl_gfx_dl.c draw_asset_override) one line per
 * 60 draws with the fade, exposure, primitive and triangle counts and the
 * MEASURED lighting term - the mean N.L of the model's normals under the
 * modelview in force. That last number is what found the 2026-09-20 defect:
 * a model whose normals were in the wrong frame lit by the ambient term
 * alone.
 *
 * NATIVE ONLY: called from the #ifndef __sgi arms of draw_watch_controller
 * and draw_watch_control_options_page.
 */
#ifndef __sgi

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ultra64.h>
#include <bondgame.h>
#include <fr.h>
#include <PR/gu.h>
#include "options.h"
#include "joy.h"
#include "dyn.h"
#include "matrixmath.h"
#include "textrelated.h"
#include "language.h"
/* The game's own float trig (the include tree's math.h shadows libm's and
 * declares only sinf / cosf / sqrtf - a libm acos or atan2 named here would
 * be an implicit int-returning declaration, measured 2026-09-20 as a probe
 * printing 1877468 degrees). */
#include "math_asinfacosf.h"
#include "math_atan2f.h"
#include "../sl_asset_override.h"
#include "../platform/sl_settings.h"
#include "../platform/sl_action.h"      /* SL_ACT_COUNT, the d-pad row's union */
#include "../platform/sl_bindings.h"

/* The platform layer (host headers only, so declared here rather than
 * included - the sl_input.h typedef is repeated verbatim). */
typedef struct {
    float left_x, left_y, right_x, right_y;
    float left_trigger, right_trigger;
    unsigned int held;
} sl_pad_visual;
extern int sl_input_pad_visual(sl_pad_visual *out);
extern int sl_input_pad_family(void);
#define SL_PAD_FAMILY_XBOX        1
#define SL_PAD_FAMILY_PLAYSTATION 2

/* options.c: the page's text helper, its two rows' state and the latch
 * machinery (the same calls the SIGHTLINE rows make). */
extern Gfx *draw_options_labels(Gfx *gdl, s32 x, s32 y, char *text, u32 colour, s32 outlined, u32 outlinecolour, s32 centre, s32 drawbg, u32 bgcolour, s32 rightalign);
extern u32  controller_options_index;
extern s32  watch_item_is_actively_selected;
extern s32  sub_GAME_7F0A4FB0(void);
extern s32  sub_GAME_7F0A4FEC(void);
extern void game_option_select_value(u32 *param_1, u32 param_2);

#define SL_WC_STICK_TILT   0.4363f   /* 25 degrees at full deflection */
#define SL_WC_PRESS_Y      -8.0f     /* a held button's depress */
#define SL_WC_ROCK         0.1745f   /* 10 degrees: d-pad, shoulders (the N64's) */
#define SL_WC_TRIGGER      0.35f     /* 20 degrees at a full pull */
#define SL_WC_HELD_TINT    1.35f     /* the highlight on a held part */
#define SL_WC_EXPOSURE_DARK 4.0f     /* the lift for a near-black model (see below) */

/* The page's perspective (options.c draw_watch_controller: guPerspective
 * with WATCH_PERSPECTIVE_FOVY / _ASPECT, 1000, 3000), restated for the label
 * projection - the two macros are options.c's own file-local ones, copied
 * with their EU variant. */
#if defined(VERSION_EU)
#define SL_WC_FOVY   52.5f
#define SL_WC_ASPECT 1.283847f
#else
#define SL_WC_FOVY   50.5f
#define SL_WC_ASPECT 1.3333334f
#endif
#define SL_WC_ZNEAR 1000.0f
#define SL_WC_ZFAR  3000.0f

/* THE LABEL COLUMNS AND THE ICONS (2026-09-20, #63 / #64 round 5, the
 * owner: "the button icons on the side are missing. Maybe we need to pull
 * the buttons out of the controller model to show on the side too"). The
 * original page's shape exactly: its text columns sit at x 0x32
 * (left-aligned) and 0x10e (right-aligned) and its button ICONS OUTSIDE
 * them, nearer the face's edge (gunfire.c watchRenderController's second
 * subdraw: the pad's own button nodes, the body hidden, each placed in the
 * look-at frame at g_1ContButtonPositions - A at x 900, L at -820, y 200 -
 * and tilted toward the camera, R_x(-60 deg) for the face buttons, +60 for
 * L / R). This page does the same with the model's own parts: for every
 * labelled control the part is drawn ALONE (sl_asset_override_emit_part,
 * about its own pivot) in an icon column outside the text, fitted to one
 * icon height, tilted toward the camera, lit by the page's rig, and
 * carrying the part's live pose - so a held A depresses and tints on the
 * pad AND on its icon. The text beside it is the ACTION alone, the
 * original's "icon + word": the family's name is the icon (the letters and
 * symbols are the model's own texture). A line that would still reach the
 * pad's projected silhouette breaks into two rows at its last space or
 * slash. The row pitch fits the icon; a column that would run past the
 * bottom tightens its pitch rather than run off the face. */
/* THE COLUMNS FOLLOW THE FACE. The watch face is a circle in framebuffer
 * pixels - MEASURED on the 960x720 frame (2026-09-20, the green extent per
 * row): centre (160, 120), half-width 149 at the middle, 137 at y 64, 125
 * at y 200; a circle of radius 148 to within a pixel - and the original
 * page's icons sit near its edge (the R at x ~276 on row 82, the A at ~287
 * on row 182). Fixed columns would put the top and bottom rows' icons on
 * the bezel, so each row's icon centre is set in from the face's edge at
 * ITS y, and its text beside the icon: the columns curve with the face,
 * as the original's do. */
#define SL_WC_FACE_CX        160.0f
#define SL_WC_FACE_CY        120.0f
#define SL_WC_FACE_R         148.0f
#define SL_WC_ICON_INSET     14.0f   /* the icon centre in from the face's edge */
#define SL_WC_TEXT_INSET     28.0f   /* the text's outer end in from the face's edge */
#define SL_WC_LABEL_PITCH    17
#define SL_WC_LABEL_PITCH_MIN 14
#define SL_WC_LABEL_GAP      4       /* between a line's end and the pad's edge */
#define SL_WC_LABEL_Y_MIN    0x40    /* below the page's two rows (0x1A, 0x2B) */
#define SL_WC_LABEL_Y_MAX    0xBE    /* the last line's top; the page bar is at ~0xDB */
#define SL_WC_LABELS_MAX     16
#define SL_WC_BODY_HALF      394.0f  /* GjoypadZ's half width, the frame both models are fitted to */
#define SL_WC_TOP_HALF       300.0f  /* the pad's half width at its top edge, where the bumpers and
                                      * triggers sit (Xbox bumper outer edge 286, DualSense 301) */
/* The icons: fitted so the part's largest face-on extent spans SL_WC_ICON_FIT
 * model units at the pad's own depth (1800 from the eye: the page's look-at
 * eye y 2000 over the pad at y 200; one framebuffer pixel there is ~7.1
 * units under the 50.5-degree perspective, so 92 units is ~13 px, the
 * text's height plus a little), the scale bounded so a tiny Options button
 * does not become a boulder and a wide bumper still fits its row. Tilts:
 * a face part (buttons, d-pad, sticks, Start / View) leans back with the
 * pad - the modern pad's own tilt (SL_WC_MODERN_TILT, 20 degrees since
 * round 6; it was a fixed 30 under the 45-degree pad), capped at 30 so the
 * letters read under any SL_PAD_TILT preview; a front part (bumpers,
 * triggers - their legends are on the FRONT face, which the pad's own view
 * hides) is turned to show that face, top edge toward the viewer, then
 * turned 180 about the view axis so the legend reads upright and unmirrored
 * (the front face is looked at from the pad's far side, where the player's
 * right is the viewer's left). */
#define SL_WC_ICON_FIT       92.0f
#define SL_WC_ICON_DEPTH     1800.0f
#define SL_WC_ICON_SCALE_MIN 0.5f
#define SL_WC_ICON_SCALE_MAX 2.5f
#define SL_WC_ICON_TILT_FACE_MAX 30.0f    /* degrees about x, leaning back like the pad */
#define SL_WC_ICON_TILT_FRONT ( 1.0472f)  /*  60 degrees about x, then 180 about y */

/* The model id for the driving pad's family, or -1. */
static int sl_wc_asset(void)
{
    switch (sl_input_pad_family()) {
    case SL_PAD_FAMILY_XBOX:        return SL_ASSET_CONTROLLER_XBOX;
    case SL_PAD_FAMILY_PLAYSTATION: return SL_ASSET_CONTROLLER_DUALSENSE;
    default:                        return -1;
    }
}

/* SL_PAD_DBG: the page's decision, once per change - family, the model id
 * it resolves to and whether that model is available - so a log says WHICH
 * arm of draw_watch_controller a frame took. Off by default. */
static int sl_wc_dbg(void)
{
    static int on = -1;
    if (on < 0) on = getenv("SL_PAD_DBG") != NULL;
    return on;
}

/* The modern pad's tilt (defined with the draw, below; the probe reads it). */
static float sl_wc_tilt_deg(void);
static void  sl_wc_tilt(Mtxf *finalmtx, Mtxf *out);

int sl_watch_controller_modern(void)
{
    int id, avail;
    id = sl_wc_asset();
    avail = id >= 0 ? sl_asset_override_available(id) : 0;
    if (sl_wc_dbg()) {
        static int last = -1;
        int now = ((id + 1) << 4) | avail;
        if (now != last) {
            last = now;
            fprintf(stderr, "sl_pad_dbg: page family=%d model=%d available=%d -> %s\n",
                    sl_input_pad_family(), id, avail,
                    avail ? "device draw" : "the N64 pad");
        }
    }
    if (id < 0)
        return 0;
    return avail;
}

/* ------------------------------------------------------------------------
 * THE TWO ROWS: BUTTON LAYOUT and STICK LAYOUT
 * --------------------------------------------------------------------- */

/* which: 0 = BUTTON LAYOUT (the registry's preset, CUSTOM when the editor
 * holds something else), 1 = STICK LAYOUT (the settings store's row). */
const char *sl_watch_layout_name(s32 which)
{
    if (which == 0)
        return sl_bindings_layout_name(sl_bindings_layout_get());
    return sl_stick_layout_name(sl_settings_get(SL_SET_PAD_STICK_LAYOUT));
}

/* One step: BUTTON LAYOUT through the presets (a step off CUSTOM lands on
 * the first / last preset and re-seeds the pad slots; choosing the preset
 * in force is a no-op), STICK LAYOUT through the four; both wrap. */
void sl_watch_layout_step(s32 which, s32 dir)
{
    if (dir == 0) return;
    if (which == 0) {
        /* The registry walks the preset order (it owns which are offered:
         * a retired one is skipped, and a step off CUSTOM lands on the
         * first / last), so this row carries no list of its own. */
        (void) sl_bindings_layout_apply(sl_bindings_layout_step(sl_bindings_layout_get(), dir > 0 ? 1 : -1));
    } else {
        int cur = sl_settings_get(SL_SET_PAD_STICK_LAYOUT);
        int next = (cur + (dir > 0 ? 1 : SL_STICK_LAYOUT_COUNT - 1)) % SL_STICK_LAYOUT_COUNT;
        sl_settings_set(SL_SET_PAD_STICK_LAYOUT, next);
    }
    if (getenv("SL_INPUT_DEBUG") != NULL)
        fprintf(stderr, "sightline watch: %s layout -> %s\n",
                which == 0 ? "button" : "stick", sl_watch_layout_name(which));
}

/* The Control Options page's two rows, in the original page's shape
 * (draw_watch_control_options_page: the label at XOFFSET_1, y 0x1A / 0x2B,
 * plain 0xFF00B0, highlighted 0xA0FFA0F0, latched white outlined; the value
 * at 0xAA in the lit 0xA0FFA0F0 the style name used). The latched row's
 * LEFT / RIGHT step the value - the latched-input shape of the SIGHTLINE
 * rows - with game_option_select_value for the sound and the stick latch.
 * The row index is the original page's controller_options_index (0 / 1),
 * so its navigation, the A / Z latch and the pointer's rows stand. */
static Gfx *sl_wc_row(Gfx *gdl, s32 row, s32 y, char *label)
{
    static char value[20];
    s32 highlighted = (s32) controller_options_index == (u32) row;
    s32 selected = highlighted && watch_item_is_actively_selected;
    const char *n = sl_watch_layout_name(row);
    s32 i = 0;

    if (selected)
    {
        s32 dir = 0;
        if (joyGetButtonsPressedThisFrame(PLAYER_1, L_CBUTTONS|L_TRIG|L_JPAD) || sub_GAME_7F0A4FB0())
            dir = -1;
        else if (joyGetButtonsPressedThisFrame(PLAYER_1, R_CBUTTONS|R_TRIG|R_JPAD) || sub_GAME_7F0A4FEC())
            dir = 1;
        if (dir != 0)
        {
            u32 dummy = 0;
            game_option_select_value(&dummy, 1);
            sl_watch_layout_step(row, dir);
            n = sl_watch_layout_name(row);
        }
    }
    while (n[i] != '\0' && i < 17) { value[i] = (char) ((n[i] >= 'A' && n[i] <= 'Z') ? n[i] + 32 : n[i]); i++; }
    value[i++] = '\n'; value[i] = '\0';

    if (selected)
        gdl = draw_options_labels(gdl, XOFFSET_1, y, label, -1, 1, 0x7000A0, 0, 0, 0x3000B0, 0);
    else if (highlighted)
        gdl = draw_options_labels(gdl, XOFFSET_1, y, label, 0xA0FFA0F0, 0, -1, 0, 0, 0x3000B0, 0);
    else
        gdl = draw_options_labels(gdl, XOFFSET_1, y, label, 0xFF00B0, 0, -1, 0, 0, 0x3000B0, 0);
    gdl = draw_options_labels(gdl, 0xAA, y, value, selected ? 0xA0FFA0F0 : 0x00FF00B0, 0, -1, 0, 0, 0x3000B0, 0);
    return gdl;
}

Gfx *sl_watch_layout_rows_draw(Gfx *gdl)
{
    static char l_button[] = "button layout\n";
    static char l_stick[]  = "stick layout\n";
    gdl = sl_wc_row(gdl, 0, 0x1A, l_button);
    gdl = sl_wc_row(gdl, 1, 0x2B, l_stick);
    return gdl;
}

/* ------------------------------------------------------------------------
 * THE LABELS AROUND THE PAD
 * --------------------------------------------------------------------- */

/* The labelled physical controls: the part whose rest pivot places the
 * label, its KIND (what the text is made of - round 6, #63 / #64: "look and
 * zoom in aren't mapped correctly" was two labels that did not say what the
 * registry does), the registry's token for a button, and the family names
 * (Xbox / PlayStation) for the controls the registry's own table does not
 * name.
 *
 *   BUTTON   the token's action(s) from the live registry, `/`-joined
 *            (RB: "NEXT WPN/ZOOM IN" - both rows of a contextual source);
 *            bound to nothing: omitted
 *   DPAD     the four directions' actions as one row, each action once,
 *            a complementary pair collapsed to its one word
 *            (sl_bindings_pair_brief: DEFAULT "WEAPONS/ZOOM"); no direction
 *            bound: omitted. Never the game's Honey word for the N64 d-pad
 *            (LOOK), which was a fact about the cartridge, not a binding
 *   STICK    two rows, never omitted: the stick's ANALOG role from the STICK
 *            LAYOUT (MOVE / LOOK; LEGACY: MOVE/TURN, LOOK/STRAFE) over its
 *            CLICK's action(s) as "CLICK <action>" - so a reader can never
 *            take the click's binding for the stick's motion (the owner read
 *            R3's "ZOOM IN" as the right stick's function)
 *   FIXED    what the control does outside the registry: Start PAUSE. (View
 *            / Create used to be FIXED too, printing MARK; #47 moved it to
 *            the registry - see the table below.) */
enum { SL_WC_BUTTON = 0, SL_WC_DPAD, SL_WC_STICK, SL_WC_FIXED };
struct sl_wc_control {
    unsigned int part;
    int          kind;
    const char  *token;
    const char  *name_xbox, *name_ps;
    int          front;     /* the icon shows the part's FRONT face (bumpers, triggers) */
};
static const struct sl_wc_control g_controls[] = {
    { SL_PART_LEFT_TRIGGER,   SL_WC_BUTTON, "pad:LT", NULL, NULL, 1 },
    { SL_PART_LEFT_SHOULDER,  SL_WC_BUTTON, "pad:LB", NULL, NULL, 1 },
    { SL_PART_RIGHT_TRIGGER,  SL_WC_BUTTON, "pad:RT", NULL, NULL, 1 },
    { SL_PART_RIGHT_SHOULDER, SL_WC_BUTTON, "pad:RB", NULL, NULL, 1 },
    { SL_PART_FACE_NORTH,     SL_WC_BUTTON, "pad:Y",  NULL, NULL, 0 },
    { SL_PART_FACE_WEST,      SL_WC_BUTTON, "pad:X",  NULL, NULL, 0 },
    { SL_PART_FACE_EAST,      SL_WC_BUTTON, "pad:B",  NULL, NULL, 0 },
    { SL_PART_FACE_SOUTH,     SL_WC_BUTTON, "pad:A",  NULL, NULL, 0 },
    { SL_PART_LEFT_STICK,     SL_WC_STICK,  "pad:LS", NULL, NULL, 0 },
    { SL_PART_RIGHT_STICK,    SL_WC_STICK,  "pad:RS", NULL, NULL, 0 },
    { SL_PART_DPAD,           SL_WC_DPAD,   "",       "D-PAD", "D-PAD", 0 },
    { SL_PART_MENU,           SL_WC_FIXED,  "",       "START", "OPTIONS", 0 },
    /* #47: BACK stopped being a fixed MARK and became a registry source, so
     * this row reads its binding like every other button instead of printing
     * a hard-coded word. Every preset puts TEXTURE SET there; a player who
     * rebinds it sees whatever they bound, and UNBOUND if they clear it. */
    { SL_PART_BACK,           SL_WC_BUTTON, "pad:BACK", NULL, NULL, 0 },
};
#define SL_WC_NCONTROLS ((int) (sizeof g_controls / sizeof g_controls[0]))
static const char *const g_dpad_tokens[4] = { "pad:DPAD_UP", "pad:DPAD_DOWN", "pad:DPAD_LEFT", "pad:DPAD_RIGHT" };

struct sl_wc_label {
    char  name[24];   /* the family's name for the control, LF-terminated (the log) */
    char  what[40];   /* the action(s), LF-terminated: the text beside the icon */
    char  row1[40], row2[40];   /* the two-row form (see sl_wc_label_text) */
    unsigned int part;
    int   front;
    float pivot[3];
    float y;          /* projected, then spread */
    int   right;      /* column */
    int   held;
    int   rows;       /* 1: `what`; 2: `row1` over `row2` */
    int   rows_fixed; /* a STICK: always its two rows */
};

/* Row-vector transform, the N64's convention: out = v * m. */
static void sl_wc_xform(const f32 v[4], f32 m[4][4], f32 out[4])
{
    s32 i;
    for (i = 0; i < 4; i++)
        out[i] = v[0] * m[0][i] + v[1] * m[1][i] + v[2] * m[2][i] + v[3] * m[3][i];
}

/* A model-space point through the page's modelview and perspective into
 * framebuffer pixels (the viewport fr.c:698-701 builds); 0 behind the eye. */
static int sl_wc_project(Mtxf *finalmtx, f32 p[4][4], const float pt[3], f32 *sx, f32 *sy)
{
    f32 v[4], c[4], d[4];
    v[0] = pt[0]; v[1] = pt[1]; v[2] = pt[2]; v[3] = 1.0f;
    sl_wc_xform(v, (f32 (*)[4]) finalmtx, c);
    sl_wc_xform(c, p, d);
    if (d[3] <= 0.0001f)
        return 0;
    *sx = (f32) viGetViewLeft() + (f32) viGetViewWidth()  * (1.0f + d[0] / d[3]) * 0.5f;
    *sy = (f32) viGetViewTop()  + (f32) viGetViewHeight() * (1.0f - d[1] / d[3]) * 0.5f;
    return 1;
}

/* Append src (up to its NUL or LF) to dst[*i], leaving room for "\n\0",
 * uppercased: the game's own strings are lowercase in the text table and
 * the watch font sets them in its small style, which beside the registry's
 * capitals read as two fonts on one line (measured on the first frame). */
static void sl_wc_cat(char *dst, int *i, int n, const char *src)
{
    int k;
    for (k = 0; src[k] != '\0' && src[k] != '\n' && *i < n - 2; k++)
        dst[(*i)++] = (char) ((src[k] >= 'a' && src[k] <= 'z') ? src[k] - 32 : src[k]);
}

/* The actions of a list, `/`-joined into dst from *i, each action once and
 * a complementary pair collapsed to its one word (sl_bindings_pair_brief).
 * `acts` is in enum order (the resolver's order), so a pair's two halves
 * are adjacent. */
static void sl_wc_cat_actions(char *dst, int *i, int n, const int *acts, int count)
{
    int a, first = 1;
    for (a = 0; a < count; a++) {
        const char *pair = (a + 1 < count) ? sl_bindings_pair_brief(acts[a], acts[a + 1]) : NULL;
        if (!first && *i < n - 2) dst[(*i)++] = '/';
        first = 0;
        if (pair != NULL) { sl_wc_cat(dst, i, n, pair); a++; }
        else              sl_wc_cat(dst, i, n, sl_bindings_action_brief(acts[a]));
    }
}

/* The stick's ANALOG role under the STICK LAYOUT in force (sl_input.c
 * map_pad_modern's table, in words). */
static const char *sl_wc_stick_role(int right)
{
    switch (sl_settings_get(SL_SET_PAD_STICK_LAYOUT)) {
    case SL_STICK_LAYOUT_SOUTHPAW:        return right ? "MOVE" : "LOOK";
    case SL_STICK_LAYOUT_LEGACY:          return right ? "LOOK/STRAFE" : "MOVE/TURN";
    case SL_STICK_LAYOUT_LEGACY_SOUTHPAW: return right ? "MOVE/TURN" : "LOOK/STRAFE";
    default:                              return right ? "LOOK" : "MOVE";
    }
}

/* The texts for one control: the family's name (for the log) and, by the
 * control's kind (see g_controls), the text beside the icon; 0 when the
 * label is omitted (a button or a d-pad bound to nothing). */
static int sl_wc_label_text(const struct sl_wc_control *c, int family, struct sl_wc_label *l)
{
    int acts[8], count = 0, i, k;
    sl_bind_source src;
    char nm[24];

    l->name[0] = l->what[0] = l->row1[0] = l->row2[0] = '\0';
    l->rows_fixed = 0;
    if (c->token[0] != '\0') {
        if (!sl_bindings_source_parse(c->token, &src)) return 0;
        sl_bindings_source_label(&src, family, nm, (int) sizeof nm);
        count = sl_bindings_actions_for_source(&src, SL_BIND_PAD, acts, 4);
        if (count > 4) count = 4;
    } else {
        sl_bind_copy(nm, (int) sizeof nm, family == SL_PAD_FAMILY_PLAYSTATION ? c->name_ps : c->name_xbox);
    }
    i = 0; sl_wc_cat(l->name, &i, (int) sizeof l->name, nm); l->name[i++] = '\n'; l->name[i] = '\0';

    i = 0;
    switch (c->kind) {
    case SL_WC_BUTTON:
        /* A PHYSICAL CONTROL IS ALWAYS SHOWN, bound or not (owner-observed
         * 2026-09-20: "the modern controller page omits the north face
         * button"). This branch used to `return 0` when the resolver found
         * no action, which dropped the row AND its icon - and since no
         * preset binds pad:Y (g_layouts: P_Y appears in none of the four),
         * the Xbox Y / DualSense TRIANGLE was simply absent from the page
         * on every one of them. The page's own list IS the part table
         * (g_controls), so the row exists; only its text was missing.
         *
         * Nothing here knows which button this is: the label comes from the
         * authoritative binding table exactly as a bound one's does
         * (sl_bindings_source_label for the family's glyph name,
         * sl_bindings_actions_for_source for the action), so binding
         * FACE_NORTH later shows that action with no further edit, and any
         * other control left unbound reads the same way.
         *
         * "UNBOUND" rather than the editors' "---": there, a dash means an
         * ACTION with no source; here it is the other direction - a control
         * with no action - and a bare dash beside a button icon reads as a
         * missing string. */
        if (count == 0) {
            sl_wc_cat(l->what, &i, (int) sizeof l->what, "UNBOUND");
            break;
        }
        sl_wc_cat_actions(l->what, &i, (int) sizeof l->what, acts, count);
        break;
    case SL_WC_DPAD: {
        /* The union of the four directions' actions, in enum order, each
         * once - so a pair split across two directions still collapses. */
        int d, n = 0, a, seen[SL_ACT_COUNT];
        memset(seen, 0, sizeof seen);
        for (d = 0; d < 4; d++) {
            int da[4], dn, j;
            if (!sl_bindings_source_parse(g_dpad_tokens[d], &src)) continue;
            dn = sl_bindings_actions_for_source(&src, SL_BIND_PAD, da, 4);
            if (dn > 4) dn = 4;
            for (j = 0; j < dn; j++) seen[da[j]] = 1;
        }
        for (a = 0; a < SL_ACT_COUNT && n < 8; a++) if (seen[a]) acts[n++] = a;
        if (n == 0) return 0;
        sl_wc_cat_actions(l->what, &i, (int) sizeof l->what, acts, n);
        break;
    }
    case SL_WC_STICK: {
        /* Row 1 the analog role, row 2 the click; `what` carries both for
         * the log. Two rows always (rows_fixed), one when the click is
         * bound to nothing. */
        int right = c->part == SL_PART_RIGHT_STICK;
        k = 0; sl_wc_cat(l->row1, &k, (int) sizeof l->row1, sl_wc_stick_role(right)); l->row1[k++] = '\n'; l->row1[k] = '\0';
        sl_wc_cat(l->what, &i, (int) sizeof l->what, sl_wc_stick_role(right));
        if (count > 0) {
            k = 0; sl_wc_cat(l->row2, &k, (int) sizeof l->row2, "CLICK ");
            sl_wc_cat_actions(l->row2, &k, (int) sizeof l->row2, acts, count);
            l->row2[k++] = '\n'; l->row2[k] = '\0';
            if (i < (int) sizeof l->what - 2) l->what[i++] = '/';
            sl_wc_cat(l->what, &i, (int) sizeof l->what, "CLICK ");
            sl_wc_cat_actions(l->what, &i, (int) sizeof l->what, acts, count);
        }
        l->rows_fixed = count > 0 ? 2 : 1;
        break;
    }
    default:
        /* #47: START is the only FIXED row left (BACK moved to the registry). */
        sl_wc_cat(l->what, &i, (int) sizeof l->what, "PAUSE");
        break;
    }
    l->what[i++] = '\n'; l->what[i] = '\0';

    /* The two-row form for the others: `what` broken at its LAST slash -
     * between two actions ("NEXT WPN/ZOOM IN" -> NEXT WPN / ZOOM IN,
     * "WEAPONS/ZOOM" -> WEAPONS / ZOOM) - or, with no slash, its last space
     * ("NEXT WPN" -> NEXT / WPN); a single word has no two-row form and
     * stays on one row whatever the room. */
    if (!l->rows_fixed) {
        int cut = -1, space = -1;
        for (i = 0; l->what[i] != '\0' && l->what[i] != '\n'; i++) {
            if (l->what[i] == '/') cut = i;
            if (l->what[i] == ' ') space = i;
        }
        if (cut < 0) cut = space;
        if (cut > 0) {
            k = 0;
            for (i = 0; i < cut && k < (int) sizeof l->row1 - 2; i++) l->row1[k++] = l->what[i];
            l->row1[k++] = '\n'; l->row1[k] = '\0';
            k = 0;
            for (i = cut + 1; l->what[i] != '\0' && l->what[i] != '\n' && k < (int) sizeof l->row2 - 2; i++)
                l->row2[k++] = l->what[i];
            l->row2[k++] = '\n'; l->row2[k] = '\0';
        }
    }
    return 1;
}

/* A line's width in framebuffer pixels, as draw_options_labels measures it. */
static s32 sl_wc_text_width(char *text)
{
    s32 h = 0, w = 0;
    textMeasure(&h, &w, text, ptrFontBankGothicChars, ptrFontBankGothic, 10);
    return w;
}

/* A label line: draw_options_labels' shape (BankGothic at line height 10,
 * the static-mode render mode, plain or outlined, left- or right-aligned)
 * with the clip box handed to textRender being the VIEW, not the measured
 * text box - the sl_watch_bindings.c wb_label rule: with the measured box
 * a glyph that reaches below line height 10 is dropped whole, and the
 * DualSense's SQUARE drew as "S UARE" on the first frame (measured
 * 2026-09-20; the Q's tail). */
static Gfx *sl_wc_text(Gfx *gdl, s32 x, s32 y, char *text, u32 colour, s32 outlined, u32 ocolour, s32 rightalign)
{
    s32 textx = rightalign ? x - sl_wc_text_width(text) : x;
    extern s32 g_WatchBackgroundGreen;

    if (g_WatchBackgroundGreen < 0xe0)
    {
        gDPSetRenderMode(gdl++, G_RM_AA_PCL_SURF, G_RM_AA_PCL_SURF2);
    }
    else
    {
        gDPSetRenderMode(gdl++, G_RM_AA_XLU_SURF, G_RM_AA_XLU_SURF2);
    }
    gDPSetRenderMode(gdl++, G_RM_AA_XLU_SURF, G_RM_AA_XLU_SURF2);
    if (outlined)
        gdl = textRenderOutlined(gdl, &textx, &y, text, ptrFontBankGothicChars, ptrFontBankGothic, colour, ocolour, viGetX(), viGetY(), 0, 10);
    else
        gdl = textRender(gdl, &textx, &y, text, ptrFontBankGothicChars, ptrFontBankGothic, colour, viGetX(), viGetY(), 0, 10);
    return gdl;
}

/* Order within a column: the top edge first (pivot z ascending - the
 * triggers and bumpers sit at -z), then down the face (y descending). */
static int sl_wc_before(const struct sl_wc_label *a, const struct sl_wc_label *b)
{
    if (a->pivot[2] != b->pivot[2]) return a->pivot[2] < b->pivot[2];
    return a->pivot[1] > b->pivot[1];
}

/* The layout: which labels, their column, their rows and their y - decided
 * once per draw, then drawn twice: the icons in the page's 3D pass and the
 * text in its 2D pass. */
struct sl_wc_layout {
    struct sl_wc_label lab[SL_WC_LABELS_MAX];
    int   n;
    f32   pitch[2];    /* per column, after the spread */
    f32   proj[4][4];
};

/* The face's half-width at a framebuffer y (0 outside the circle). */
static f32 sl_wc_face_half(f32 y)
{
    f32 dy = y - SL_WC_FACE_CY, r2 = SL_WC_FACE_R * SL_WC_FACE_R - dy * dy;
    return r2 > 0.0f ? sqrtf(r2) : 0.0f;
}

/* Where a label's icon centre and its text's outer end sit for a row at
 * y (the text's top): in from the face's edge at the icon's own height. */
static void sl_wc_row_x(const struct sl_wc_label *l, f32 pitch, f32 *icon_x, f32 *icon_y, f32 *text_x)
{
    f32 cy = l->y + 5.0f + (l->rows == 2 ? pitch * 0.5f : 0.0f);
    f32 half = sl_wc_face_half(cy);
    *icon_y = cy;
    if (l->right) {
        *icon_x = SL_WC_FACE_CX + half - SL_WC_ICON_INSET;
        *text_x = SL_WC_FACE_CX + half - SL_WC_TEXT_INSET;
    } else {
        *icon_x = SL_WC_FACE_CX - half + SL_WC_ICON_INSET;
        *text_x = SL_WC_FACE_CX - half + SL_WC_TEXT_INSET;
    }
}

/* One column: order by the rest pivots, spread the projected rows top to
 * bottom at the pitch, and if the column ran past the bottom pull it up and
 * tighten the pitch (down to the icon's height) until it fits - the labels
 * sit beside their parts where the room allows, never on each other and
 * never off the face. py[] holds each label's projected y (the spread
 * starts from it every time it runs). */
static void sl_wc_spread(struct sl_wc_layout *L, int side, const f32 *py)
{
    struct sl_wc_label *lab = L->lab;
    int idx[SL_WC_LABELS_MAX], k = 0, lines = 0, i, j;
    f32 y, pitch = (f32) SL_WC_LABEL_PITCH;

    for (i = 0; i < L->n; i++) if (lab[i].right == side) { idx[k++] = i; lines += lab[i].rows; lab[i].y = py[i]; }
    for (i = 1; i < k; i++) {                    /* insertion sort, k <= 8 */
        int t = idx[i];
        for (j = i; j > 0 && sl_wc_before(&lab[t], &lab[idx[j - 1]]); j--) idx[j] = idx[j - 1];
        idx[j] = t;
    }
    if (lines > 1 && (f32) (lines - 1) * pitch > (f32) (SL_WC_LABEL_Y_MAX - SL_WC_LABEL_Y_MIN))
        pitch = (f32) (SL_WC_LABEL_Y_MAX - SL_WC_LABEL_Y_MIN) / (f32) (lines - 1);
    if (pitch < (f32) SL_WC_LABEL_PITCH_MIN) pitch = (f32) SL_WC_LABEL_PITCH_MIN;
    y = (f32) SL_WC_LABEL_Y_MIN;
    for (i = 0; i < k; i++) {
        if (lab[idx[i]].y < y) lab[idx[i]].y = y;
        y = lab[idx[i]].y + pitch * (f32) lab[idx[i]].rows;
    }
    if (k > 0) {
        f32 last = lab[idx[k - 1]].y + pitch * (f32) (lab[idx[k - 1]].rows - 1);
        if (last > (f32) SL_WC_LABEL_Y_MAX) {
            f32 up = last - (f32) SL_WC_LABEL_Y_MAX;
            for (i = 0; i < k; i++) lab[idx[i]].y -= up;
            for (i = k - 1; i > 0; i--)           /* keep the pitch from the bottom up */
                if (lab[idx[i - 1]].y > lab[idx[i]].y - pitch * (f32) lab[idx[i - 1]].rows)
                    lab[idx[i - 1]].y = lab[idx[i]].y - pitch * (f32) lab[idx[i - 1]].rows;
        }
    }
    L->pitch[side] = pitch;
}

static void sl_wc_layout(struct sl_wc_layout *L, int id, Mtxf *finalmtx, unsigned int held)
{
    struct sl_wc_label *lab = L->lab;
    const struct sl_amdl *m = sl_asset_override_get_model(id);
    f32 py[SL_WC_LABELS_MAX], ex[SL_WC_LABELS_MAX], body_y0, body_y1;
    u16 pn;
    int family = sl_input_pad_family();
    int n = 0, i, side, again;

    L->n = 0;
    L->pitch[0] = L->pitch[1] = (f32) SL_WC_LABEL_PITCH;
    if (m == NULL || m->part == NULL)
        return;
    guPerspectiveF(L->proj, &pn, SL_WC_FOVY, SL_WC_ASPECT, SL_WC_ZNEAR, SL_WC_ZFAR, 1.0f);

    for (i = 0; i < SL_WC_NCONTROLS && n < SL_WC_LABELS_MAX; i++) {
        int pi = sl_asset_override_part_index(id, g_controls[i].part);
        f32 sx, sy, ey, edge[3];
        if (pi < 0) continue;                       /* this pad has no such part */
        /* A row is omitted only where the CONTROL has no text of its own -
         * a d-pad with nothing on any direction. A button with no action
         * reads UNBOUND and keeps its icon (see sl_wc_label_text). */
        if (!sl_wc_label_text(&g_controls[i], family, &lab[n])) continue;
        lab[n].part  = g_controls[i].part;
        lab[n].front = g_controls[i].front;
        lab[n].pivot[0] = m->part[pi].pivot[0];
        lab[n].pivot[1] = m->part[pi].pivot[1];
        lab[n].pivot[2] = m->part[pi].pivot[2];
        if (!sl_wc_project(finalmtx, L->proj, lab[n].pivot, &sx, &sy)) continue;
        py[n] = sy;
        lab[n].right = lab[n].pivot[0] >= 0.0f;
        lab[n].rows = lab[n].rows_fixed ? lab[n].rows_fixed : 1;
        /* The snapshot flags the d-pad by direction (DPAD_UP..RIGHT), the
         * one-part d-pad label lights on any of them. */
        if (g_controls[i].part == SL_PART_DPAD)
            lab[n].held = (held & ((1u << SL_PART_DPAD_UP) | (1u << SL_PART_DPAD_DOWN)
                                   | (1u << SL_PART_DPAD_LEFT) | (1u << SL_PART_DPAD_RIGHT))) != 0;
        else
            lab[n].held = (held >> g_controls[i].part) & 1u;
        /* The pad's silhouette at this part's depth: the body's edge (+-394,
         * or +-300 at the top edge where the bumpers and triggers sit),
         * projected at the pivot's y and z. */
        edge[0] = lab[n].front ? SL_WC_TOP_HALF : SL_WC_BODY_HALF;
        if (!lab[n].right) edge[0] = -edge[0];
        edge[1] = lab[n].pivot[1];
        edge[2] = lab[n].pivot[2];
        if (!sl_wc_project(finalmtx, L->proj, edge, &ex[n], &ey))
            ex[n] = lab[n].right ? -1.0e9f : 1.0e9f;
        n++;
    }
    L->n = n;

    /* The pad's projected vertical span: the body's bounds (its bbox
     * corners about its pivot) through the same projection. A row above or
     * below it cannot reach the pad whatever its width. */
    {
        int bi = sl_asset_override_part_index(id, SL_PART_BODY);
        body_y0 = -1.0e9f; body_y1 = 1.0e9f;
        if (bi >= 0) {
            const struct sl_amdl_part *b = &m->part[bi];
            int c;
            body_y0 = 1.0e9f; body_y1 = -1.0e9f;
            for (c = 0; c < 8; c++) {
                f32 pt[3], sx, sy;
                pt[0] = b->pivot[0] + ((c & 1) ? b->hi[0] : b->lo[0]);
                pt[1] = b->pivot[1] + ((c & 2) ? b->hi[1] : b->lo[1]);
                pt[2] = b->pivot[2] + ((c & 4) ? b->hi[2] : b->lo[2]);
                if (!sl_wc_project(finalmtx, L->proj, pt, &sx, &sy)) continue;
                if (sy < body_y0) body_y0 = sy;
                if (sy > body_y1) body_y1 = sy;
            }
        }
    }

    /* Spread each column on one row per label, then let a line that would
     * still reach the pad's silhouette from its row's text position break
     * into two rows (if it has a two-row form) and spread again with the
     * rows it now takes. */
    for (side = 0; side < 2; side++) sl_wc_spread(L, side, py);
    again = 0;
    for (i = 0; i < n; i++) {
        f32 ix, iy, tx, avail;
        if (lab[i].row1[0] == '\0' || lab[i].rows_fixed) continue;
        if (lab[i].y + 10.0f < body_y0 || lab[i].y > body_y1) continue;   /* clear of the pad */
        sl_wc_row_x(&lab[i], L->pitch[lab[i].right], &ix, &iy, &tx);
        avail = (lab[i].right ? tx - ex[i] : ex[i] - tx) - (f32) SL_WC_LABEL_GAP;
        if ((f32) sl_wc_text_width(lab[i].what) > avail) { lab[i].rows = 2; again |= 1 << lab[i].right; }
    }
    for (side = 0; side < 2; side++) if (again & (1 << side)) sl_wc_spread(L, side, py);

    if (sl_wc_dbg()) {
        static unsigned calls;
        if ((calls++ % 60u) == 0u) {
            fprintf(stderr, "sl_pad_dbg: labels id=%d family=%d n=%d pitch=%.1f/%.1f:", id, family, n, L->pitch[0], L->pitch[1]);
            for (i = 0; i < n; i++) {
                int ln = (int) strlen(lab[i].name), lw = (int) strlen(lab[i].what);
                fprintf(stderr, " [%s %.*s: %.*s y=%.0f rows=%d%s]", lab[i].right ? "R" : "L",
                        ln > 0 ? ln - 1 : 0, lab[i].name, lw > 0 ? lw - 1 : 0, lab[i].what,
                        lab[i].y, lab[i].rows, lab[i].held ? " held" : "");
            }
            fprintf(stderr, "\n");
        }
    }
}

/* The text pass (the page's 2D pass, after microcode_constructor). */
static Gfx *sl_wc_labels_text(Gfx *gdl, const struct sl_wc_layout *L)
{
    int i;
    for (i = 0; i < L->n; i++) {
        const struct sl_wc_label *l = &L->lab[i];
        f32 ix, iy, tx;
        s32 x, y0 = (s32) (l->y + 0.5f);
        sl_wc_row_x(l, L->pitch[l->right], &ix, &iy, &tx);
        x = (s32) (tx + 0.5f);
        u32 colour = l->held ? (u32) -1 : 0xAA00B0;
        s32 outlined = l->held ? 1 : 0;
        u32 ocolour = l->held ? 0x7000A0 : (u32) -1;
        if (l->rows == 1) {
            gdl = sl_wc_text(gdl, x, y0, (char *) l->what, colour, outlined, ocolour, l->right);
        } else {
            gdl = sl_wc_text(gdl, x, y0, (char *) l->row1, colour, outlined, ocolour, l->right);
            gdl = sl_wc_text(gdl, x, y0 + (s32) L->pitch[l->right], (char *) l->row2, colour, outlined, ocolour, l->right);
        }
    }
    return gdl;
}

/* ------------------------------------------------------------------------
 * THE ICONS: each labelled part drawn alone beside its label
 * --------------------------------------------------------------------- */

/* The fixed-point conversion scale and its save / restore pair, defined in
 * src/game/matrixmath.c and absent from matrixmath.h (the game's own callers
 * use them undeclared - tree-wide grep 2026-09-20, no header names them). */
extern f32  D_80032310[2];
extern void matrix_4x4_7F058C64(void);
extern void matrix_4x4_7F058C88(void);

/* A framebuffer pixel at the icon depth back to a point in the look-at
 * frame's WORLD: the inverse of sl_wc_project's mapping through the page's
 * perspective, then eye -> world through the look-at's own basis (its
 * columns are the camera's right, up and back; its eye is where the page
 * puts it, options.c:3707: (-5, 2000, -168) looking down -y, up -z). */
static void sl_wc_unproject(Mtxf *lookat, f32 p[4][4], f32 sx, f32 sy, f32 depth, f32 out[3])
{
    f32 nx = 2.0f * (sx - (f32) viGetViewLeft()) / (f32) viGetViewWidth()  - 1.0f;
    f32 ny = 1.0f - 2.0f * (sy - (f32) viGetViewTop())  / (f32) viGetViewHeight();
    /* clip = eye * P; P[0][0] and P[1][1] scale x and y, w = -eye.z = depth */
    f32 ex = nx * depth / p[0][0];
    f32 ey = ny * depth / p[1][1];
    f32 ez = -depth;
    f32 *L = (f32 *) lookat;      /* L[r*4+c]: column c of rows 0..2 is a basis axis, row 3 is -eye.axis */
    f32 eye[3];
    int k;
    /* eye position: -(row3) expressed back through the (orthonormal) basis */
    for (k = 0; k < 3; k++)
        eye[k] = -(L[12] * L[k * 4 + 0] + L[13] * L[k * 4 + 1] + L[14] * L[k * 4 + 2]);
    for (k = 0; k < 3; k++)
        out[k] = eye[k] + ex * L[k * 4 + 0] + ey * L[k * 4 + 1] + ez * L[k * 4 + 2];
}

static Gfx *sl_wc_icons_draw(Gfx *gdl, int id, const struct sl_wc_layout *L, int fade)
{
    const struct sl_amdl *m = sl_asset_override_get_model(id);
    Mtxf lookat, S, R, R2, T, t1, t2, t3, final;
    coord3d pos;
    int i;

    if (m == NULL || m->part == NULL || L->n == 0)
        return gdl;
    matrix_4x4_set_lookat_target(&lookat, -5.0f, 2000.0f, -168.0f, -5.0f, 0.0f, -168.0f, 0.0f, 0.0f, -1.0f);

    for (i = 0; i < L->n; i++) {
        const struct sl_wc_label *l = &L->lab[i];
        int pi = sl_asset_override_part_index(id, l->part);
        const struct sl_amdl_part *pt;
        f32 ext_x, ext_y, ext_z, big, s, cx, cy, world[3];
        Mtx *mtx;
        if (pi < 0) continue;
        pt = &m->part[pi];
        ext_x = pt->hi[0] - pt->lo[0];
        ext_y = pt->hi[1] - pt->lo[1];
        ext_z = pt->hi[2] - pt->lo[2];
        if (l->front) { big = ext_x * 0.6f; if (ext_y > big) big = ext_y; if (ext_z > big) big = ext_z; }
        else          { big = ext_x;        if (ext_z > big) big = ext_z; }
        if (big <= 0.0f) continue;
        s = SL_WC_ICON_FIT / big;
        if (s < SL_WC_ICON_SCALE_MIN) s = SL_WC_ICON_SCALE_MIN;
        if (s > SL_WC_ICON_SCALE_MAX) s = SL_WC_ICON_SCALE_MAX;

        /* The icon's centre: in from the face's edge on the label's row (its
         * text is 10 high; a two-row label centres on both rows). */
        {
            f32 tx;
            sl_wc_row_x(l, L->pitch[l->right], &cx, &cy, &tx);
        }
        sl_wc_unproject(&lookat, (f32 (*)[4]) L->proj, cx, cy, SL_WC_ICON_DEPTH, world);
        pos.f[0] = world[0]; pos.f[1] = world[1]; pos.f[2] = world[2];

        /* v * S * R * T * lookat: scale about the pivot, tilt, place, view.
         * matrix_4x4_multiply(lhs, rhs) applies rhs first. */
        matrix_4x4_set_identity(&S);
        S.m[0][0] = S.m[1][1] = S.m[2][2] = s;
        if (l->front) {
            matrix_4x4_set_rotation_around_x(SL_WC_ICON_TILT_FRONT, &R);
            matrix_4x4_set_rotation_around_y(3.14159265f, &R2);
            matrix_4x4_multiply(&R2, &R, &t1);            /* R then R2 */
            matrix_4x4_multiply(&t1, &S, &t2);            /* S then (R, R2) */
        } else {
            float lean = sl_wc_tilt_deg();
            if (lean > SL_WC_ICON_TILT_FACE_MAX) lean = SL_WC_ICON_TILT_FACE_MAX;
            if (lean < 0.0f) lean = 0.0f;
            matrix_4x4_set_rotation_around_x(-lean * (3.14159265f / 180.0f), &R);
            matrix_4x4_multiply(&R, &S, &t2);             /* S then R */
        }
        matrix_4x4_set_identity_and_position(&pos, &T);
        matrix_4x4_multiply(&T, &t2, &t3);                /* ... then T */
        matrix_4x4_multiply(&lookat, &t3, &final);        /* ... then the view */

        mtx = dynAllocateMatrix();
        matrix_4x4_7F058C64();                            /* the unit-scale bracket (see the pad's draw) */
        matrix_4x4_f32_to_s32((f32 (*)[4]) &final, (s32 (*)[4]) mtx);
        matrix_4x4_7F058C88();
        gSPMatrix(gdl++, osVirtualToPhysical(mtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gdl = (Gfx *) sl_asset_override_emit_part(gdl, id, l->part, fade);
    }
    return gdl;
}

/* ------------------------------------------------------------------------
 * THE MODEL
 * --------------------------------------------------------------------- */

static void sl_wc_press(int id, unsigned int part, unsigned int held)
{
    struct sl_amdl_pose p;
    if ((held & (1u << part)) == 0)
        return;
    memset(&p, 0, sizeof p);
    p.move[1] = SL_WC_PRESS_Y;
    p.tint[0] = p.tint[1] = p.tint[2] = SL_WC_HELD_TINT;
    (void) sl_asset_override_pose_set(id, part, &p);
}

static void sl_wc_rock(int id, unsigned int part, float rx, float rz, int held)
{
    struct sl_amdl_pose p;
    memset(&p, 0, sizeof p);
    p.rot[0] = rx;
    p.rot[2] = rz;
    p.tint[0] = p.tint[1] = p.tint[2] = held ? SL_WC_HELD_TINT : 1.0f;
    (void) sl_asset_override_pose_set(id, part, &p);
}

/* ------------------------------------------------------------------------
 * THE FACING PROBE (SL_PAD_DBG, #63 round 5: "xbox is still not facing the
 * camera correctly")
 *
 * What "facing" IS, measured rather than argued: the model-frame face
 * normal of each pad taken through the page's finalmtx into EYE space,
 * where +z is toward the camera (the look-at's third column is the
 * camera's back), and its angle to the view axis. The N64 pad's face
 * normal is +y in GjoypadZ's frame - measured on the ROM model
 * (scratch modern5/n64pad.py: the face buttons' winding normals are
 * (0, 0.273, 0) to three places, the plane through their pivots pitched
 * -2.4 degrees) - and it is drawn under this same finalmtx, so the first
 * line is the N64 pad's own facing for any spin and pitch. The second is
 * the MODERN model's, from its own mesh: the mean of the stored normals of
 * its four face buttons (re-based into the same frame by the importer).
 * The angle between the two is how far the modern pad is from the N64 pad
 * on the page; zero means "faces exactly as the N64 pad does".
 *
 * Called by draw_watch_controller for both arms (options.c), once per 60
 * calls, off by default; no behaviour attached.
 * --------------------------------------------------------------------- */
static void sl_wc_face_normal(int id, float out[3])
{
    static float cache[SL_ASSET_ID_COUNT][3];
    static int   have[SL_ASSET_ID_COUNT];
    const struct sl_amdl *m;
    unsigned i, e;
    double s[3] = { 0.0, 0.0, 0.0 };
    unsigned n = 0;

    out[0] = 0.0f; out[1] = 1.0f; out[2] = 0.0f;
    if (id < 0 || id >= SL_ASSET_ID_COUNT) return;
    if (have[id]) { out[0] = cache[id][0]; out[1] = cache[id][1]; out[2] = cache[id][2]; return; }
    m = sl_asset_override_get_model(id);
    if (m == NULL || m->nrm == NULL || m->part == NULL) return;
    for (i = 0; i < m->nprim; i++) {
        unsigned pid;
        if (m->prim[i].part == SL_PART_NONE) continue;
        pid = m->part[m->prim[i].part].id;
        if (pid != SL_PART_FACE_SOUTH && pid != SL_PART_FACE_EAST
            && pid != SL_PART_FACE_WEST && pid != SL_PART_FACE_NORTH) continue;
        for (e = m->prim[i].first; e < m->prim[i].first + m->prim[i].count; e++) {
            const float *nn = m->nrm + (size_t) m->idx[e] * 3u;
            /* the up-facing half only: a button's side walls cancel */
            if (nn[1] <= 0.0f) continue;
            s[0] += nn[0]; s[1] += nn[1]; s[2] += nn[2]; n++;
        }
    }
    if (n > 0) {
        f32 len = sqrtf((f32) (s[0] * s[0] + s[1] * s[1] + s[2] * s[2]));
        if (len > 0.0f) { out[0] = (float) s[0] / len; out[1] = (float) s[1] / len; out[2] = (float) s[2] / len; }
    }
    cache[id][0] = out[0]; cache[id][1] = out[1]; cache[id][2] = out[2];
    have[id] = 1;
}

static f32 sl_wc_deg_acos(f32 c)
{
    if (c > 1.0f) c = 1.0f;
    if (c < -1.0f) c = -1.0f;
    return acosf(c) * (180.0f / 3.14159265f);
}

void sl_watch_controller_probe(Mtxf *finalmtx)
{
    static unsigned calls;
    int id;
    f32 n64[3] = { 0.0f, 1.0f, 0.0f }, mod[3], e64[3], emod[3], l64, lmod, dot, tilt;
    int i;

    if (!sl_wc_dbg()) return;
    if ((calls++ % 60u) != 0u) return;
    id = sl_wc_asset();
    /* a direction through the upper 3x3, row-vector: e = n * M */
    for (i = 0; i < 3; i++)
        e64[i] = n64[0] * finalmtx->m[0][i] + n64[1] * finalmtx->m[1][i] + n64[2] * finalmtx->m[2][i];
    l64 = sqrtf(e64[0] * e64[0] + e64[1] * e64[1] + e64[2] * e64[2]);
    if (l64 > 0.0f) { e64[0] /= l64; e64[1] /= l64; e64[2] /= l64; }
    tilt = atan2f(e64[1], e64[2]) * (180.0f / 3.14159265f);
    if (tilt < 0.0f) tilt = -tilt;
    fprintf(stderr, "sl_pad_dbg: facing N64 pad (GjoypadZ face +y): eye (%.3f %.3f %.3f) -> %.1f deg from the view axis (%s, %.1f deg %s)\n",
            e64[0], e64[1], e64[2], sl_wc_deg_acos(e64[2]),
            e64[2] > 0.0f ? "toward the camera" : "AWAY from the camera",
            tilt, e64[1] >= 0.0f ? "seen from above" : "seen from below");
    if (id >= 0 && sl_asset_override_available(id)) {
        /* The modern model draws under ITS tilt (sl_wc_tilt), so its facing
         * is measured under that matrix - the N64 pad's line above stays
         * the page's own 45. */
        Mtxf tilted;
        sl_wc_tilt(finalmtx, &tilted);
        sl_wc_face_normal(id, mod);
        for (i = 0; i < 3; i++)
            emod[i] = mod[0] * tilted.m[0][i] + mod[1] * tilted.m[1][i] + mod[2] * tilted.m[2][i];
        lmod = sqrtf(emod[0] * emod[0] + emod[1] * emod[1] + emod[2] * emod[2]);
        if (lmod > 0.0f) { emod[0] /= lmod; emod[1] /= lmod; emod[2] /= lmod; }
        dot = e64[0] * emod[0] + e64[1] * emod[1] + e64[2] * emod[2];
        fprintf(stderr, "sl_pad_dbg: facing model %d face (mesh, model frame %.3f %.3f %.3f) at tilt %.0f: eye (%.3f %.3f %.3f) -> %.1f deg from the view axis; %.1f deg from the N64 pad's face\n",
                id, mod[0], mod[1], mod[2], sl_wc_tilt_deg(), emod[0], emod[1], emod[2],
                sl_wc_deg_acos(emod[2]), sl_wc_deg_acos(dot));
    }
}

/* THE MODERN PAD'S TILT (round 6 of #63 / #64, 2026-09-20). The page
 * pitches whatever it draws 45 degrees from face-on (options.c:3688, the
 * N64 pad's own presentation, kept for the N64 pad). Round 5 established
 * that the modern models FACE exactly as the N64 pad does under that
 * matrix (the probe below: within 2 degrees) and that what the owner saw
 * as "still not facing the camera correctly" was the 45-degree pitch
 * itself on a modern pad's shape - and offered previews at 45 / 20 / 0.
 * The owner's replay: "Xbox still needs to be pitched more too ... Looks
 * like ps5 can be tilted better too" - so the modern pad now draws at
 * SL_WC_MODERN_TILT, the middle preview, for BOTH families; the labels and
 * the icons follow (the columns are placed from the projected pivots and
 * the icons lean with the pad, below). SL_PAD_TILT=<whole degrees> stays as
 * the developer override of that default. */
#define SL_WC_MODERN_TILT 20.0f

static float sl_wc_tilt_deg(void)
{
    static int checked;
    static float deg = SL_WC_MODERN_TILT;
    if (!checked) {
        const char *e = getenv("SL_PAD_TILT");
        checked = 1;
        /* atoi, not atof: atof read "0" as 45745600.0 here (measured
         * 2026-09-20 - a double return that this TU's declarations do not
         * carry across the i686 call; sl_teleport.c declares it by hand for
         * the same reason). Whole degrees are all the seam needs. */
        if (e != NULL && e[0] != '\0') deg = (float) atoi(e);
    }
    return deg;
}

/* The page's finalmtx re-pitched for the modern pad: the page pitches the
 * model by R_x(-45 deg) first, so the difference is undone BEFORE it:
 * v * R_x(45 - deg) * finalmtx. */
static void sl_wc_tilt(Mtxf *finalmtx, Mtxf *out)
{
    Mtxf r;
    float deg = sl_wc_tilt_deg();
    matrix_4x4_set_rotation_around_x((45.0f - deg) * (3.14159265f / 180.0f), &r);
    matrix_4x4_multiply(finalmtx, &r, out);
    if (sl_wc_dbg()) {
        static unsigned calls;
        if ((calls++ % 60u) == 0u)
            fprintf(stderr, "sl_pad_dbg: modern tilt deg=%.1f r.row1=(%.3f %.3f %.3f) in.row1=(%.3f %.3f %.3f) out.row1=(%.3f %.3f %.3f) out.row3=(%.0f %.0f %.0f)\n",
                    deg, r.m[1][0], r.m[1][1], r.m[1][2],
                    finalmtx->m[1][0], finalmtx->m[1][1], finalmtx->m[1][2],
                    out->m[1][0], out->m[1][1], out->m[1][2], out->m[3][0], out->m[3][1], out->m[3][2]);
    }
}

Gfx *sl_watch_controller_draw(Gfx *gdl, Mtxf *finalmtx, s32 green)
{
    int id = sl_wc_asset();
    sl_pad_visual v;
    Mtx *mtx;
    int fade;
    unsigned int held;
    float scale_in_force;
    struct sl_wc_layout layout;
    Mtxf tilted;

    if (id < 0 || !sl_asset_override_available(id))
        return gdl;
    sl_wc_tilt(finalmtx, &tilted);
    finalmtx = &tilted;

    sl_asset_override_pose_reset(id);
    /* The Xbox pad's texture is near black (RGB ~32 mean) and vanishes on
     * the watch's dark green face at plain modulate; the DualSense's shell
     * is white (231). So the exposure follows the model - 4x for the black
     * pad, 1x for the white one - through the renderer's one bounded knob;
     * no second light rig, no material edit.
     *
     * RE-MEASURED 2026-09-20 (#63, defect round 2). The 2026-09-19 numbers
     * were taken on models whose NORMAL data was in the wrong frame (the
     * package builder re-based positions, not normals - see the controllers
     * README), so every visible polygon was lit by the ambient term alone:
     * the DualSense's "white" first draw was 4x on a model at 0.27
     * brightness, and at 1x that same model drew as a dark silhouette (RGB
     * 69), which is what the owner saw as nothing drawn. With the importer
     * re-basing the normals the shell reads 221 at 1x and the Xbox shell
     * 108..126 at 4x, both with shading. The knob values stand. */
    sl_asset_override_exposure_set(id, id == SL_ASSET_CONTROLLER_XBOX ? SL_WC_EXPOSURE_DARK : 1.0f);
    if (!sl_input_pad_visual(&v))
        memset(&v, 0, sizeof v);
    held = v.held;

    /* The sticks: the N64 stick's sense - push right, the top leans to +x
     * (about z, negative); push up (SDL y negative), the top leans to -z,
     * the pad's top edge (about x, negative). */
    sl_wc_rock(id, SL_PART_LEFT_STICK,  v.left_y  * SL_WC_STICK_TILT,
               -v.left_x  * SL_WC_STICK_TILT, (held >> SL_PART_LEFT_STICK) & 1u);
    sl_wc_rock(id, SL_PART_RIGHT_STICK, v.right_y * SL_WC_STICK_TILT,
               -v.right_x * SL_WC_STICK_TILT, (held >> SL_PART_RIGHT_STICK) & 1u);

    /* The buttons. */
    sl_wc_press(id, SL_PART_FACE_SOUTH, held);
    sl_wc_press(id, SL_PART_FACE_EAST,  held);
    sl_wc_press(id, SL_PART_FACE_WEST,  held);
    sl_wc_press(id, SL_PART_FACE_NORTH, held);
    sl_wc_press(id, SL_PART_MENU,       held);
    sl_wc_press(id, SL_PART_BACK,       held);
    sl_wc_press(id, SL_PART_GUIDE,      held);
    sl_wc_press(id, SL_PART_MUTE,       held);
    sl_wc_press(id, SL_PART_TOUCHPAD,   held);
    sl_wc_press(id, SL_PART_DPAD_UP,    held);
    sl_wc_press(id, SL_PART_DPAD_DOWN,  held);
    sl_wc_press(id, SL_PART_DPAD_LEFT,  held);
    sl_wc_press(id, SL_PART_DPAD_RIGHT, held);

    /* The one-part d-pad rocks toward the held direction: up dips the top
     * edge (-z side) - about x, negative; right dips the +x side - about z,
     * negative (a point at +x, y 0 goes to y = x sin(theta), so negative). */
    {
        float rx = 0.0f, rz = 0.0f;
        int any = 0;
        if (held & (1u << SL_PART_DPAD_UP))    { rx -= SL_WC_ROCK; any = 1; }
        if (held & (1u << SL_PART_DPAD_DOWN))  { rx += SL_WC_ROCK; any = 1; }
        if (held & (1u << SL_PART_DPAD_RIGHT)) { rz -= SL_WC_ROCK; any = 1; }
        if (held & (1u << SL_PART_DPAD_LEFT))  { rz += SL_WC_ROCK; any = 1; }
        if (any)
            sl_wc_rock(id, SL_PART_DPAD, rx, rz, 1);
    }

    /* Shoulders rock down at their front edge while held; triggers swing
     * with the analog pull (their paddle hangs below the pivot and swings
     * back into the pad). */
    if (held & (1u << SL_PART_LEFT_SHOULDER))
        sl_wc_rock(id, SL_PART_LEFT_SHOULDER,  SL_WC_ROCK, 0.0f, 1);
    if (held & (1u << SL_PART_RIGHT_SHOULDER))
        sl_wc_rock(id, SL_PART_RIGHT_SHOULDER, SL_WC_ROCK, 0.0f, 1);
    if (v.left_trigger > 0.0f)
        sl_wc_rock(id, SL_PART_LEFT_TRIGGER,  v.left_trigger  * SL_WC_TRIGGER, 0.0f,
                   (held >> SL_PART_LEFT_TRIGGER) & 1u);
    if (v.right_trigger > 0.0f)
        sl_wc_rock(id, SL_PART_RIGHT_TRIGGER, v.right_trigger * SL_WC_TRIGGER, 0.0f,
                   (held >> SL_PART_RIGHT_TRIGGER) & 1u);

    /* The page's own modelview (spin, pitch, position, look-at), then the
     * bridge command - the boot screens' idiom (front.c:2018). The page's
     * green level fades the model in with the page, as the N64 pad's env
     * colour does.
     *
     * THE CONVERSION SCALE (#63 defect round 3). matrix_4x4_f32_to_s32 is
     * not a plain float-to-fixed conversion: it multiplies every element but
     * the w column by D_80032310[0], which bg.c:1091 sets to 65536 x the
     * level's VISIBILITY scale on load (levelinfotable: Dam, Surface and
     * Surface 2 carry 0.2; the other seventeen 1.0; the front end 1.0). The
     * N64 pad's own path converts at UNIT scale - watchRenderController
     * brackets its render_pos conversion with matrix_4x4_7F058C64 /
     * matrix_4x4_7F058C88 (gunfire.c:2239-2247), which save the scale, set
     * 65536 and restore - because the watch's geometry is authored in the
     * watch's own units, not the level's. This draw did not, so on Dam the
     * whole finalmtx (rotation AND the look-at translation) arrived at the
     * renderer x0.2: the pad shrank to a fifth and its eye-space depth went
     * from 1800 to 360, in front of the page's own near plane (guPerspective
     * near 1000), and the model was clipped away entirely - the blank page
     * with the hands showing through (measured: mv=[0.200 ...] on Dam,
     * [1.000 ...] on Facility, same build, same window). Same bracket. */
    mtx = dynAllocateMatrix();
    scale_in_force = D_80032310[0] * (1.0f / 65536.0f);
    matrix_4x4_7F058C64();
    matrix_4x4_f32_to_s32((f32 (*)[4]) finalmtx, (s32 (*)[4]) mtx);
    matrix_4x4_7F058C88();
    gSPMatrix(gdl++, osVirtualToPhysical(mtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    fade = (int) green + 0x20;
    if (fade > 0xFF) fade = 0xFF;
    if (fade < 0x40) fade = 0x40;
    if (sl_wc_dbg()) {
        static unsigned calls;
        if ((calls++ % 60u) == 0u)
            fprintf(stderr, "sl_pad_dbg: watch emit id=%d green=%d fade=%d held=%08x"
                            " L(%.2f,%.2f) R(%.2f,%.2f) T(%.2f,%.2f) mtx-scale=%.3f (converted at 1.000) call=%u\n",
                    id, (int) green, fade, held, v.left_x, v.left_y, v.right_x, v.right_y,
                    v.left_trigger, v.right_trigger, scale_in_force, calls);
    }
    gdl = (Gfx *) sl_asset_override_emit(gdl, id, fade);

    /* The labels' layout, then the ICONS in the same 3D pass (each part
     * alone, its own modelview, the pad's projection still loaded - the
     * original page's second subdraw), then the text in the page's 2D pass
     * - the same switch the original page makes before its own labels
     * (sub_GAME_7F0A9AB8 opens with microcode_constructor). */
    sl_wc_layout(&layout, id, finalmtx, held);
    {
        /* SL_PAD_ICONS=0 (developer seam, off by default): the labels
         * without their icons - the A/B half of the icons' cost measurement. */
        static int icons = -1;
        if (icons < 0) { const char *e = getenv("SL_PAD_ICONS"); icons = !(e != NULL && e[0] == '0'); }
        if (icons)
            gdl = sl_wc_icons_draw(gdl, id, &layout, fade);
    }
    gdl = microcode_constructor(gdl);
    gdl = sl_wc_labels_text(gdl, &layout);
    return gdl;
}

#endif /* !__sgi */
