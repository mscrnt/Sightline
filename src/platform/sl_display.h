/**
 * sl_display.h - the native display shape (#45): the selectable aspect ratio
 * and THE ONE content-viewport fit every consumer shares.
 *
 * WHAT IT DECIDES. The player picks a SHAPE - 4:3, 16:9 or 32:9 - at the
 * CURRENT VERTICAL SIZE: widescreen means a WIDER view at the same height,
 * never a strip inside the old window (owner contract, 2026-09-18). In
 * windowed mode the SDL backend (src/gfx/sl_gfx_sdl.c sdl_apply_aspect)
 * therefore keeps the window's height and sets its width to height x
 * aspect - 960x720 -> 1280x720 -> 2560x720 - live on a change and at the
 * first frame from the persisted setting; SL_WINDOW_SIZE names only the
 * initial window and the aspect wins on the width. Shape and size are two
 * settings and are never conflated: 16:9 is not 1920x1080.
 *
 * From the framebuffer that results (or the one that cannot be resized: a
 * fullscreen, fullscreen-desktop or maximised window) this file derives ONE
 * set of rectangles, in window pixels:
 *
 *   content   the selected aspect fitted inside the framebuffer, centred:
 *             the largest rectangle of that shape that fits. In windowed
 *             mode that IS the window. For a fixed framebuffer of another
 *             shape: wider than the shape -> pillarbox (full height,
 *             bars left and right); narrower (taller) -> the FULL WIDTH is
 *             kept and the rest letterboxed top and bottom, because the
 *             alternative would crop the wide view; the content is never
 *             narrower than the framebuffer. Nothing is ever stretched.
 *   safe      the game's LOGICAL framebuffer shape (320x240 or 440x330,
 *             both 4:3) fitted inside `content`, centred - i.e. scaled by
 *             the content HEIGHT, the same pixel size the 4:3 image has at
 *             that height, centred horizontally in the wider content. This
 *             is where the 4:3 image lands: the HUD, the watch, the front
 *             end's text, the 2D overlays and the crosshair. At 4:3 it IS
 *             the content rect.
 *   k         content.w / safe.w, >= 1: how much wider the 3D view is than
 *             the logical image. 1 at 4:3, 4/3 at 16:9, 8/3 at 32:9.
 *
 * and the renderer (src/gfx/sl_gfx_dl.c) applies them: the 3D projection's x
 * scale divided by k over a viewport widened by k (the same image inside
 * `safe`, more world in the bands to either side - the vertical composition
 * is untouched), the 2D ortho mapped so that logical [0,w] lands on `safe`,
 * and the pointer layers (src/native/sl_menu_pointer.c, sl_watch_pointer.c)
 * read `safe` back through sl_gfx_present_rect. No second copy of the fit
 * exists anywhere; that is the point of this file.
 *
 * WHAT IT DOES NOT TOUCH. Nothing in src/game reads the aspect. The game's
 * own projection (fr.c:709, fovy 60 vertical over the 4:3 logical viewport),
 * its frustum planes, its "on screen" flags, aim, auto-aim, spread, guard
 * perception and every script test keep the 4:3 view they had, at every
 * aspect: the wider view is a RENDER fact only. That is what keeps the trace
 * harness and the gameplay byte-identical (rules 1 and 5).
 *
 * HOST-CLEAN like sl_settings.h: plain ints and doubles, so src/platform,
 * src/gfx, src/native and the self-test can all include it.
 */
#ifndef SL_DISPLAY_H
#define SL_DISPLAY_H

/* The persisted ids are APPEND-ONLY: 2 = 32:9 was already in the owner's
 * config.ini when 21:9 was added on 2026-09-18, so 21:9 took the next id
 * rather than the slot between. The DISPLAY row orders them by width
 * (sl_aspect_by_order); nothing else cares about the numbering. */
enum sl_aspect_ratio {
    SL_ASPECT_4_3  = 0,      /* the accepted presentation, unchanged */
    SL_ASPECT_16_9 = 1,
    SL_ASPECT_32_9 = 2,
    SL_ASPECT_21_9 = 3,
    SL_ASPECT_COUNT
};

/* The ids in display order, narrowest first: 4:3, 16:9, 21:9, 32:9. */
int sl_aspect_by_order(int column);
/* The display column of an id (the inverse). */
int sl_aspect_order_of(int aspect);

/* The persisted selection (sl_settings, key aspect_ratio), clamped to the
 * enum: an inactive store, a missing key or a bad value answer 4:3. THIS is
 * the one accessor; the renderer and the UI both use it. */
int sl_aspect_ratio(void);

/* Set the selection (write-on-change through the store). Out-of-range is
 * refused. */
void sl_aspect_ratio_set(int aspect);

/* "4:3", "16:9", "32:9" - a fixed string, never NULL (bad ids read "4:3"). */
const char *sl_aspect_name(int aspect);

/* The numeric ratio width/height: 4/3, 16/9, 32/9 (bad ids: 4/3). */
double sl_aspect_value(int aspect);

/* The rectangles above. Inputs: the framebuffer size in pixels, the logical
 * framebuffer size the display list declares (320x240, 440x330 - any
 * positive pair), and the aspect id. Output rectangles use a TOP-LEFT origin
 * (x, y, w, h), which is what the pointer APIs use; the renderer flips y for
 * GL itself. Returns 0 (and leaves *out untouched) for a degenerate input. */
struct sl_display_rects {
    int    content[4];       /* the selected aspect fitted in the framebuffer */
    int    safe[4];          /* the logical 4:3 image fitted in content */
    double k;                /* content.w / safe.w (>= 1) */
};
int sl_display_rects(int fb_w, int fb_h, int scr_w, int scr_h, int aspect,
                     struct sl_display_rects *out);

/* The one fit both rectangles are built with: the largest rectangle of
 * shape `ratio` (w/h) that fits inside fb_w x fb_h, centred, integer pixels.
 * Exposed for the self-test and for callers that need only the shape. */
void sl_display_fit(int fb_w, int fb_h, double ratio, int *x, int *y, int *w, int *h);

/* Logical <-> physical pointer transforms over a rect (top-left origin):
 * a window pixel to fractions of the rect and back. Pure helpers so the
 * pointer mapping is testable without SDL; sl_menu_pointer_uv does the same
 * arithmetic against the rect the renderer reports. */
int  sl_display_to_uv(const int rect[4], int px, int py, double *u, double *v);
void sl_display_from_uv(const int rect[4], double u, double v, int *px, int *py);

/* THE POINTER'S LOGICAL POSITION, EXTENDED (the widescreen cursor repair).
 * A window pixel -> logical units of scr_w x scr_h mapped onto `safe`, so a
 * pixel in a band beside the safe rect lands OUTSIDE [0, scr_w] - which is
 * where the renderer's 2D ortho can still draw it (the content rect). The
 * pixel is first confined to `content`: the drawn cursor never leaves the
 * content rect. Returns 2 for a pixel inside the safe rect (a hit position -
 * the same test sl_menu_pointer_uv makes, so the two can never disagree),
 * 1 inside the content rect only (drawable, never a hit), 0 outside the
 * content rect (confined to its edge; drawable, never a hit), -1 for a
 * degenerate input (nothing written). Single-precision, in the order the
 * front end's own mapping computes (u = (px - x) / w, then u * scr_w), so a
 * pixel inside the safe rect yields the game cursor's own value. */
int  sl_display_pointer_logical(const int safe[4], const int content[4],
                                int px, int py, float scr_w, float scr_h,
                                float *lx, float *ly);

/* Is the selected aspect in force for THIS frame? 0 while the game runs
 * split-screen (two or more players): the per-player viewports share one
 * 4:3 layout and are not widened - see docs; the fit still applies (a 4:3
 * content rect, never a stretch). Reads the player count through the
 * native query (sl_game_query.c). */
int sl_aspect_active(void);

/* ---- FIELD OF VIEW (#45, owner scope addition 2026-09-18) ----------------
 *
 * STORED: the VERTICAL field of view - the engine's own invariant, the fovy
 * fr.c:709 hands guPerspectiveF - as `fov_vertical` in HUNDREDTHS of a
 * degree, default 6000 = 60.00 exactly (FOV_Y_F), so the default is a no-op
 * and 4:3 stays byte-identical. Range 3598..7756, which is h16 60..110 (below).
 *
 * DISPLAYED: the 16:9-equivalent HORIZONTAL field of view in whole degrees,
 * the players' convention:  h16 = 2 * atan(tan(v / 2) * 16 / 9). The default
 * 60.00 vertical reads as 91 (91.49 rounded); a step is one displayed degree
 * and stores the converted vertical, except that stepping onto the default's
 * displayed value stores the exact default (so a round trip returns to
 * 60.00, not 59.79).
 *
 * APPLIED by the renderer only, on the player's WORLD projection (the one
 * fr.c's viSetupCurrentPlayerView loads, named to the renderer by
 * sl_gfx_note_world_projection; the watch, the front end and the title keep
 * theirs): clip x and y scaled by s = tan(30) / tan(v_eff / 2) - a wider
 * vertical FOV makes the 60-degree image smaller inside the same viewport
 * and more world shows around it. Nothing in the game changes: aim,
 * auto-aim, spread and the look rate are Rare's at every setting (the mouse
 * stays degrees per count; the sniper zoom's own fovy-scaled rate is its
 * own) - and the crosshair sight, a 2D texrect the game places in its
 * 60-degree screen space, is moved by the same s about the view centre
 * (gunfire.c gunDrawSight, native arm) so it stays over the aim ray.
 *
 * THE HORIZONTAL CAP: the total horizontal field of view 2 * atan(tan(v/2)
 * * ratio) never exceeds SL_FOV_H_CAP_DEG = 140 - beyond that a rectilinear
 * projection's edge magnification (1 / cos^2 of the half angle: 8.5x at
 * 70 degrees) is past what any shipped game tolerates. When the slider and
 * the aspect would exceed it, the EFFECTIVE vertical FOV for that aspect is
 * reduced so the total lands on the cap (sl_display_fov_effective), and the
 * `sl_display:` heartbeat says `cap=engaged`. At the default the cap touches
 * nothing: 4:3 75.2, 16:9 91.5, 21:9 106.8, 32:9 128.1 total. */
#define SL_FOV_V_DEFAULT   6000     /* hundredths: 60.00 = FOV_Y_F */
#define SL_FOV_H16_MIN     60
#define SL_FOV_H16_MAX     110
#define SL_FOV_H16_DEFAULT 91       /* round(91.49) */
#define SL_FOV_H_CAP_DEG   140.0

/* The stored vertical FOV in hundredths (the store; default when inactive
 * or bad) and its setter (refused outside the range). */
int    sl_fov_vertical(void);
void   sl_fov_vertical_set(int hundredths);
/* Conversions (degrees, double): vertical <-> 16:9-equivalent horizontal. */
double sl_fov_h16_from_vertical(double v_deg);
double sl_fov_vertical_from_h16(double h16_deg);
/* The displayed value (whole degrees) and a step of +/- 1 displayed degree
 * from the current setting, with the default snap and the range clamp. */
int    sl_fov_h16_displayed(void);
void   sl_fov_step(int delta);
/* The SLIDER view (#50, the bar rows): a displayed degree set directly
 * (clamped to the range, the default's exact vertical when it is 91 - the
 * one path sl_fov_step also takes), the fill fraction (h16 - MIN) / (MAX -
 * MIN), and the set from a fraction, rounded to the nearest whole degree. */
void   sl_fov_h16_set(int h16);
float  sl_fov_fraction(void);
void   sl_fov_set_fraction(float t);
/* The effective vertical FOV (degrees) at an aspect after the cap; *capped
 * (optional) says whether the cap engaged. */
double sl_display_fov_effective(int aspect, int *capped);
/* The renderer's scale for the world projection: tan(30) / tan(v_eff / 2),
 * 1.0 at the default, 1.0 when the store is inactive or in the front end
 * (no level: the title stage), 1.0 in split-screen. */
double sl_display_fov_scale(void);

/* The window width the selection asks for at a given client height:
 * round(height x ratio) - 720 -> 960 / 1280 / 2560. The SDL backend applies
 * it in windowed mode (sl_gfx_sdl.c sdl_apply_aspect). */
int sl_display_window_width(int height, int aspect);

/* How much wider / taller the WORLD VIEW is than the game's own 4:3,
 * 60-degree view, by selection: x = k / s (k = the aspect's content / safe,
 * s = the FOV scale), y = 1 / s; both 1 when not active. The two numbers the
 * game's room traversal reads to widen its root rectangle (src/game/bg.c,
 * declared there by extern). */
float sl_view_scale(void);
float sl_view_scale_y(void);

#endif /* SL_DISPLAY_H */
