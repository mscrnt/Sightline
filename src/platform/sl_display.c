/**
 * sl_display.c - the native display shape (#45). See sl_display.h.
 *
 * WHY src/platform. Pure host arithmetic over the settings store, no game
 * type, no GL: the same class as sl_settings.c, so src/gfx (host class) can
 * include the header and the self-test can run without a window. The one
 * fact it needs from the game - the player count, for the split-screen
 * gate - comes through the native query layer (sl_game_query.c), the shape
 * every other platform-side question of the game takes.
 *
 * THE FIT. Given a framebuffer W x H and a ratio r = w/h, the largest
 * centred rectangle of that ratio:
 *
 *     if W/H >= r   (the framebuffer is at least as wide as the shape)
 *         h = H, w = round(H * r)            -> pillarbox, x = (W - w) / 2
 *     else
 *         w = W, h = round(W / r)            -> letterbox, y = (H - h) / 2
 *
 * Integer pixels, rounded to nearest, so 1280x720 at 16:9 is exactly
 * 1280x720 and 960x720 at 4:3 is exactly 960x720 (the accepted window -
 * the content and the safe rect are then the whole window and k is 1, which
 * is what makes the 4:3 selection byte-identical to the build before #45).
 *
 * SELF-TEST: tools/windows/displaytest.ps1 (this file + sl_settings.c with
 * -DSL_DISPLAY_SELFTEST).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "sl_settings.h"
#include "sl_display.h"

#ifdef SL_DISPLAY_SELFTEST
static int s_test_players = 1;
static int s_test_in_level = 1;
static int sl_game_player_count(void) { return s_test_players; }
static int sl_game_in_level(void) { return s_test_in_level; }
#else
/* src/native/sl_game_query.c: how many players the game is running, 1 in
 * the front end and before any player exists; and whether a level (not the
 * title stage) is running - the FOV applies to the world view only. */
extern int sl_game_player_count(void);
extern int sl_game_in_level(void);
#endif

/* Indexed by id (append-only, see the header). */
static const char   *s_names[SL_ASPECT_COUNT]  = { "4:3", "16:9", "32:9", "21:9" };
static const double  s_values[SL_ASPECT_COUNT] = { 4.0 / 3.0, 16.0 / 9.0, 32.0 / 9.0, 21.0 / 9.0 };
/* Display order, narrowest first. */
static const int     s_order[SL_ASPECT_COUNT]  = { SL_ASPECT_4_3, SL_ASPECT_16_9, SL_ASPECT_21_9, SL_ASPECT_32_9 };

#define SL_PI 3.14159265358979323846
#define SL_DEG2RAD(d) ((d) * SL_PI / 180.0)
#define SL_RAD2DEG(r) ((r) * 180.0 / SL_PI)

int sl_aspect_by_order(int column)
{
    if (column < 0 || column >= SL_ASPECT_COUNT)
        return SL_ASPECT_4_3;
    return s_order[column];
}

int sl_aspect_order_of(int aspect)
{
    int i;
    for (i = 0; i < SL_ASPECT_COUNT; i++)
        if (s_order[i] == aspect) return i;
    return 0;
}

int sl_aspect_ratio(void)
{
    int a = sl_settings_get(SL_SET_ASPECT_RATIO);
    if (a < 0 || a >= SL_ASPECT_COUNT)
        return SL_ASPECT_4_3;
    return a;
}

void sl_aspect_ratio_set(int aspect)
{
    if (aspect < 0 || aspect >= SL_ASPECT_COUNT)
        return;
    sl_settings_set(SL_SET_ASPECT_RATIO, aspect);
}

const char *sl_aspect_name(int aspect)
{
    if (aspect < 0 || aspect >= SL_ASPECT_COUNT)
        return s_names[SL_ASPECT_4_3];
    return s_names[aspect];
}

double sl_aspect_value(int aspect)
{
    if (aspect < 0 || aspect >= SL_ASPECT_COUNT)
        return s_values[SL_ASPECT_4_3];
    return s_values[aspect];
}

int sl_aspect_active(void)
{
    return sl_game_player_count() <= 1;
}

/* THE ONE VALUE THE GAME SIDE READS (src/game/bg.c, the room traversal's
 * root rectangle; the extern idiom the file already uses for
 * sl_portal_conservative). Exact by selection, not by window: the content
 * rect is the selected shape and the safe rect is 4:3 inside it, so their
 * width ratio is selected / (4/3) whatever the window measures - 1 at 4:3,
 * 4/3 at 16:9, 8/3 at 32:9 - and 1 whenever the aspect is not active (the
 * store inactive: trace replay and headless health; split-screen). */
float sl_view_scale(void)
{
    if (!sl_aspect_active())
        return 1.0f;
    return (float) ((sl_aspect_value(sl_aspect_ratio()) / (4.0 / 3.0)) / sl_display_fov_scale());
}

float sl_view_scale_y(void)
{
    if (!sl_aspect_active())
        return 1.0f;
    return (float) (1.0 / sl_display_fov_scale());
}

/* ---- field of view --------------------------------------------------- */

int sl_fov_vertical(void)
{
    int v = sl_settings_get(SL_SET_FOV_VERTICAL);
    if (v < 3598 || v > 7756)
        return SL_FOV_V_DEFAULT;
    return v;
}

void sl_fov_vertical_set(int hundredths)
{
    if (hundredths < 3598 || hundredths > 7756)
        return;
    sl_settings_set(SL_SET_FOV_VERTICAL, hundredths);
}

double sl_fov_h16_from_vertical(double v_deg)
{
    return SL_RAD2DEG(2.0 * atan(tan(SL_DEG2RAD(v_deg) * 0.5) * (16.0 / 9.0)));
}

double sl_fov_vertical_from_h16(double h16_deg)
{
    return SL_RAD2DEG(2.0 * atan(tan(SL_DEG2RAD(h16_deg) * 0.5) * (9.0 / 16.0)));
}

int sl_fov_h16_displayed(void)
{
    int v = sl_fov_vertical();
    if (v == SL_FOV_V_DEFAULT)
        return SL_FOV_H16_DEFAULT;
    return (int) floor(sl_fov_h16_from_vertical((double) v / 100.0) + 0.5);
}

void sl_fov_h16_set(int h)
{
    int v;
    if (h < SL_FOV_H16_MIN) h = SL_FOV_H16_MIN;
    if (h > SL_FOV_H16_MAX) h = SL_FOV_H16_MAX;
    if (h == SL_FOV_H16_DEFAULT) {
        v = SL_FOV_V_DEFAULT;               /* the exact original, not 59.79 */
    } else {
        v = (int) floor(sl_fov_vertical_from_h16((double) h) * 100.0 + 0.5);
        if (v < 3598) v = 3598;
        if (v > 7756) v = 7756;
    }
    sl_fov_vertical_set(v);
}

void sl_fov_step(int delta)
{
    sl_fov_h16_set(sl_fov_h16_displayed() + (delta < 0 ? -1 : delta > 0 ? 1 : 0));
}

/* The slider bars' two calls (#50): the displayed degree's place in
 * MIN..MAX, and the nearest whole degree to a fraction of it - the same
 * values the step keys reach, one per degree, the default's exact vertical
 * at 91 (the slider cannot land on 59.79 any more than the keys can). */
float sl_fov_fraction(void)
{
    return (float) (sl_fov_h16_displayed() - SL_FOV_H16_MIN)
         / (float) (SL_FOV_H16_MAX - SL_FOV_H16_MIN);
}

void sl_fov_set_fraction(float t)
{
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    sl_fov_h16_set(SL_FOV_H16_MIN + (int) (t * (float) (SL_FOV_H16_MAX - SL_FOV_H16_MIN) + 0.5f));
}

double sl_display_fov_effective(int aspect, int *capped)
{
    double v = (double) sl_fov_vertical() / 100.0;
    double ratio = sl_aspect_value(aspect);
    double total = SL_RAD2DEG(2.0 * atan(tan(SL_DEG2RAD(v) * 0.5) * ratio));
    if (capped) *capped = 0;
    if (total > SL_FOV_H_CAP_DEG) {
        v = SL_RAD2DEG(2.0 * atan(tan(SL_DEG2RAD(SL_FOV_H_CAP_DEG) * 0.5) / ratio));
        if (capped) *capped = 1;
    }
    return v;
}

double sl_display_fov_scale(void)
{
    double v;
    if (!sl_settings_active() || !sl_aspect_active() || !sl_game_in_level())
        return 1.0;
    if (sl_fov_vertical() == SL_FOV_V_DEFAULT) {
        int capped = 0;
        v = sl_display_fov_effective(sl_aspect_ratio(), &capped);
        if (!capped)
            return 1.0;                     /* the default: exactly no-op */
    } else {
        v = sl_display_fov_effective(sl_aspect_ratio(), NULL);
    }
    return tan(SL_DEG2RAD(30.0)) / tan(SL_DEG2RAD(v) * 0.5);
}

static int round_pos(double v)
{
    return (int) (v + 0.5);
}

int sl_display_window_width(int height, int aspect)
{
    if (height <= 0)
        return 0;
    return round_pos((double) height * sl_aspect_value(aspect));
}

void sl_display_fit(int fb_w, int fb_h, double ratio, int *x, int *y, int *w, int *h)
{
    int fw, fh;
    if (fb_w <= 0 || fb_h <= 0 || ratio <= 0.0) {
        *x = *y = 0; *w = fb_w > 0 ? fb_w : 0; *h = fb_h > 0 ? fb_h : 0;
        return;
    }
    if ((double) fb_w / (double) fb_h >= ratio) {
        fh = fb_h;
        fw = round_pos((double) fb_h * ratio);
        if (fw > fb_w) fw = fb_w;
    } else {
        fw = fb_w;
        fh = round_pos((double) fb_w / ratio);
        if (fh > fb_h) fh = fb_h;
    }
    if (fw < 1) fw = 1;
    if (fh < 1) fh = 1;
    *x = (fb_w - fw) / 2;
    *y = (fb_h - fh) / 2;
    *w = fw;
    *h = fh;
}

int sl_display_rects(int fb_w, int fb_h, int scr_w, int scr_h, int aspect,
                     struct sl_display_rects *out)
{
    double logical, want;
    int cx, cy, cw, ch, sx, sy, sw, sh;

    if (fb_w <= 0 || fb_h <= 0 || scr_w <= 0 || scr_h <= 0 || out == NULL)
        return 0;

    logical = (double) scr_w / (double) scr_h;
    want    = sl_aspect_value(aspect);
    /* The selection never narrows the image below its logical shape: a 4:3
     * logical framebuffer at the 4:3 selection is the identity, and no
     * selection is narrower than 4:3. Guarded anyway, so k stays >= 1. */
    if (want < logical) want = logical;

    sl_display_fit(fb_w, fb_h, want, &cx, &cy, &cw, &ch);
    sl_display_fit(cw, ch, logical, &sx, &sy, &sw, &sh);

    out->content[0] = cx; out->content[1] = cy; out->content[2] = cw; out->content[3] = ch;
    out->safe[0] = cx + sx; out->safe[1] = cy + sy; out->safe[2] = sw; out->safe[3] = sh;
    out->k = (double) cw / (double) sw;
    return 1;
}

int sl_display_to_uv(const int rect[4], int px, int py, double *u, double *v)
{
    double fu, fv;
    if (rect[2] <= 0 || rect[3] <= 0)
        return 0;
    fu = (double) (px - rect[0]) / (double) rect[2];
    fv = (double) (py - rect[1]) / (double) rect[3];
    *u = fu;
    *v = fv;
    return fu >= 0.0 && fu <= 1.0 && fv >= 0.0 && fv <= 1.0;
}

void sl_display_from_uv(const int rect[4], double u, double v, int *px, int *py)
{
    *px = rect[0] + (int) (u * (double) rect[2] + 0.5);
    *py = rect[1] + (int) (v * (double) rect[3] + 0.5);
}

int sl_display_pointer_logical(const int safe[4], const int content[4],
                               int px, int py, float scr_w, float scr_h,
                               float *lx, float *ly)
{
    float fu, fv;
    int in_content = 1;

    if (safe[2] <= 0 || safe[3] <= 0 || content[2] <= 0 || content[3] <= 0
        || scr_w <= 0.0f || scr_h <= 0.0f || lx == NULL || ly == NULL)
        return -1;

    /* Confined to the content rect first: a pointer in a pillarbox bar (a
     * fixed framebuffer of another shape) draws at the content edge. */
    if (px < content[0])                   { px = content[0];                   in_content = 0; }
    if (px > content[0] + content[2] - 1)  { px = content[0] + content[2] - 1;  in_content = 0; }
    if (py < content[1])                   { py = content[1];                   in_content = 0; }
    if (py > content[1] + content[3] - 1)  { py = content[1] + content[3] - 1;  in_content = 0; }

    /* The front end's own arithmetic (sl_menu_pointer_uv, then
     * sl_menu_pointer_apply's left + u * width with left = 0). */
    fu = (float) (px - safe[0]) / (float) safe[2];
    fv = (float) (py - safe[1]) / (float) safe[3];
    *lx = fu * scr_w;
    *ly = fv * scr_h;
    if (!in_content)
        return 0;
    if (fu < 0.0f || fu > 1.0f || fv < 0.0f || fv > 1.0f)
        return 1;
    return 2;
}

/* ====================================================================== */
#ifdef SL_DISPLAY_SELFTEST

static int g_fail, g_checks;
static void ck(int cond, const char *what)
{
    g_checks++;
    if (!cond) { g_fail++; printf("  FAIL  %s\n", what); }
}

static int rect_is(const int r[4], int x, int y, int w, int h)
{
    return r[0] == x && r[1] == y && r[2] == w && r[3] == h;
}

static int near(double a, double b)
{
    return a - b < 1e-9 && b - a < 1e-9;
}

int main(void)
{
    struct sl_display_rects r;
    int x, y, w, h, px, py;
    double u, v;
    char path[512];
    const char *tmpdir = getenv("TEMP");
    if (tmpdir == NULL || tmpdir[0] == '\0') tmpdir = ".";
    sprintf(path, "%s\\sl_display_selftest\\config.ini", tmpdir);
    remove(path);

    /* 1. names and values */
    ck(strcmp(sl_aspect_name(SL_ASPECT_4_3), "4:3") == 0, "name 4:3");
    ck(strcmp(sl_aspect_name(SL_ASPECT_16_9), "16:9") == 0, "name 16:9");
    ck(strcmp(sl_aspect_name(SL_ASPECT_32_9), "32:9") == 0, "name 32:9");
    ck(strcmp(sl_aspect_name(SL_ASPECT_21_9), "21:9") == 0, "name 21:9 (id 3, appended after 32:9)");
    ck(strcmp(sl_aspect_name(-1), "4:3") == 0 && strcmp(sl_aspect_name(4), "4:3") == 0, "bad id names 4:3");
    ck(near(sl_aspect_value(SL_ASPECT_4_3), 4.0 / 3.0), "value 4:3 = 4/3");
    ck(near(sl_aspect_value(SL_ASPECT_16_9), 16.0 / 9.0), "value 16:9 = 16/9");
    ck(near(sl_aspect_value(SL_ASPECT_21_9), 21.0 / 9.0), "value 21:9 = 21/9");
    ck(near(sl_aspect_value(SL_ASPECT_32_9), 32.0 / 9.0), "value 32:9 = 32/9");
    ck(near(sl_aspect_value(9), 4.0 / 3.0), "bad id value 4/3");
    ck(sl_aspect_by_order(0) == SL_ASPECT_4_3 && sl_aspect_by_order(1) == SL_ASPECT_16_9
       && sl_aspect_by_order(2) == SL_ASPECT_21_9 && sl_aspect_by_order(3) == SL_ASPECT_32_9,
       "display order 4:3, 16:9, 21:9, 32:9 over the append-only ids 0, 1, 3, 2");
    ck(sl_aspect_order_of(SL_ASPECT_32_9) == 3 && sl_aspect_order_of(SL_ASPECT_21_9) == 2
       && sl_aspect_by_order(9) == SL_ASPECT_4_3, "order inverse, bad column -> 4:3");
    ck(sl_display_window_width(720, SL_ASPECT_21_9) == 1680, "720 tall at 21:9 -> 1680 wide");

    /* 2. the accessor over the store: inactive -> 4:3; set/get; persistence;
     *    malformed / out-of-range -> 4:3 */
    ck(sl_aspect_ratio() == SL_ASPECT_4_3, "inactive store -> 4:3");
    {
        char env[600];
        sprintf(env, "SL_CONFIG=%s", path);
        putenv(env);
    }
    sl_settings_init();
    ck(sl_settings_active(), "store active on the scratch file");
    ck(sl_aspect_ratio() == SL_ASPECT_4_3, "missing file -> 4:3");
    sl_aspect_ratio_set(SL_ASPECT_16_9);
    ck(sl_aspect_ratio() == SL_ASPECT_16_9, "set 16:9 reads back");
    sl_aspect_ratio_set(SL_ASPECT_32_9);
    ck(sl_aspect_ratio() == SL_ASPECT_32_9, "set 32:9 reads back");
    sl_aspect_ratio_set(5);
    ck(sl_aspect_ratio() == SL_ASPECT_32_9, "set 5 refused");
    sl_aspect_ratio_set(-1);
    ck(sl_aspect_ratio() == SL_ASPECT_32_9, "set -1 refused");
    {
        FILE *f = fopen(path, "r"); char buf[2048]; size_t n = 0;
        if (f) { n = fread(buf, 1, sizeof buf - 1, f); fclose(f); }
        buf[n] = '\0';
        ck(strstr(buf, "aspect_ratio=2\n") != NULL, "aspect_ratio=2 persisted");
    }
    sl_aspect_ratio_set(SL_ASPECT_4_3);

    /* 3. the fit */
    sl_display_fit(960, 720, 4.0 / 3.0, &x, &y, &w, &h);
    ck(x == 0 && y == 0 && w == 960 && h == 720, "960x720 at 4:3 is the whole window");
    sl_display_fit(1280, 720, 16.0 / 9.0, &x, &y, &w, &h);
    ck(x == 0 && y == 0 && w == 1280 && h == 720, "1280x720 at 16:9 is the whole window");
    sl_display_fit(2560, 720, 32.0 / 9.0, &x, &y, &w, &h);
    ck(x == 0 && y == 0 && w == 2560 && h == 720, "2560x720 at 32:9 is the whole window");
    sl_display_fit(1280, 720, 4.0 / 3.0, &x, &y, &w, &h);
    ck(x == 160 && y == 0 && w == 960 && h == 720, "4:3 in a 16:9 window pillarboxes to 960x720 at x 160");
    sl_display_fit(960, 720, 16.0 / 9.0, &x, &y, &w, &h);
    ck(x == 0 && y == 90 && w == 960 && h == 540, "16:9 in a 4:3 window letterboxes to 960x540 at y 90");
    sl_display_fit(960, 720, 32.0 / 9.0, &x, &y, &w, &h);
    ck(x == 0 && y == 225 && w == 960 && h == 270, "32:9 in a 4:3 window letterboxes to 960x270 at y 225");
    sl_display_fit(1920, 1080, 16.0 / 9.0, &x, &y, &w, &h);
    ck(w == 1920 && h == 1080, "1920x1080 at 16:9 whole (aspect, not resolution)");
    sl_display_fit(2560, 1440, 16.0 / 9.0, &x, &y, &w, &h);
    ck(w == 2560 && h == 1440, "2560x1440 at 16:9 whole (same shape, different size)");
    sl_display_fit(3840, 1080, 32.0 / 9.0, &x, &y, &w, &h);
    ck(w == 3840 && h == 1080, "3840x1080 at 32:9 whole");
    sl_display_fit(0, 720, 4.0 / 3.0, &x, &y, &w, &h);
    ck(w == 0, "degenerate width -> empty");

    /* 4. the rects: matching, wider and narrower framebuffers */
    ck(sl_display_rects(960, 720, 320, 240, SL_ASPECT_4_3, &r), "rects 960x720 4:3");
    ck(rect_is(r.content, 0, 0, 960, 720) && rect_is(r.safe, 0, 0, 960, 720) && near(r.k, 1.0),
       "4:3 at 960x720: content = safe = window, k 1 (the baseline)");
    ck(sl_display_rects(960, 720, 440, 330, SL_ASPECT_4_3, &r) && near(r.k, 1.0)
       && rect_is(r.safe, 0, 0, 960, 720), "4:3 with the 440x330 front-end logical size: k 1");
    ck(sl_display_rects(1280, 720, 320, 240, SL_ASPECT_16_9, &r), "rects 1280x720 16:9");
    ck(rect_is(r.content, 0, 0, 1280, 720) && rect_is(r.safe, 160, 0, 960, 720) && near(r.k, 4.0 / 3.0),
       "16:9 at 1280x720: content the window, safe 960x720 centred, k 4/3");
    ck(sl_display_rects(2560, 720, 320, 240, SL_ASPECT_32_9, &r), "rects 2560x720 32:9");
    ck(rect_is(r.content, 0, 0, 2560, 720) && rect_is(r.safe, 800, 0, 960, 720) && near(r.k, 8.0 / 3.0),
       "32:9 at 2560x720: content the window, safe 960x720 centred, k 8/3");
    ck(sl_display_rects(1280, 720, 320, 240, SL_ASPECT_4_3, &r)
       && rect_is(r.content, 160, 0, 960, 720) && rect_is(r.safe, 160, 0, 960, 720) && near(r.k, 1.0),
       "4:3 in a 16:9 window: pillarboxed content, k 1");
    ck(sl_display_rects(960, 720, 320, 240, SL_ASPECT_32_9, &r)
       && rect_is(r.content, 0, 225, 960, 270) && rect_is(r.safe, 300, 225, 360, 270) && near(r.k, 8.0 / 3.0),
       "32:9 in a 4:3 window: letterboxed content 960x270, safe 360x270 centred, k 8/3");
    ck(sl_display_rects(1280, 720, 320, 240, SL_ASPECT_32_9, &r)
       && rect_is(r.content, 0, 180, 1280, 360) && rect_is(r.safe, 400, 180, 480, 360) && near(r.k, 8.0 / 3.0),
       "32:9 in a 16:9 window: letterboxed 1280x360, safe 480x360, k 8/3");
    ck(sl_display_rects(2560, 720, 320, 240, SL_ASPECT_16_9, &r)
       && rect_is(r.content, 640, 0, 1280, 720) && rect_is(r.safe, 800, 0, 960, 720) && near(r.k, 4.0 / 3.0),
       "16:9 in a 32:9 window: pillarboxed 1280x720, safe 960x720, k 4/3");
    ck(sl_display_rects(1920, 1080, 320, 240, SL_ASPECT_16_9, &r) && near(r.k, 4.0 / 3.0)
       && rect_is(r.safe, 240, 0, 1440, 1080), "16:9 at 1920x1080: k 4/3, safe 1440x1080 (shape, not size)");
    ck(sl_display_rects(1280, 720, 320, 240, 7, &r) && near(r.k, 1.0) && rect_is(r.content, 160, 0, 960, 720),
       "bad aspect id reads 4:3");
    ck(!sl_display_rects(0, 720, 320, 240, 0, &r), "degenerate framebuffer refused");
    ck(!sl_display_rects(1280, 720, 0, 240, 0, &r), "degenerate logical size refused");

    /* 5. the expected horizontal expansion: tan(hfov/2) = tan(30) * 4/3 * k.
     *    4:3 -> 75.2 deg, 16:9 -> 91.5 deg, 32:9 -> 128.1 deg; the vertical
     *    60 degrees is the same number in all three. */
    {
        double t = 0.57735026919 * (4.0 / 3.0);     /* tan(30 deg) * 4/3 */
        ck(near(t * 1.0, t), "4:3 horizontal extent = the baseline");
        ck(sl_display_rects(1280, 720, 320, 240, SL_ASPECT_16_9, &r) && near(t * r.k, 0.57735026919 * 16.0 / 9.0),
           "16:9 horizontal extent = tan(30) * 16/9");
        ck(sl_display_rects(2560, 720, 320, 240, SL_ASPECT_32_9, &r) && near(t * r.k, 0.57735026919 * 32.0 / 9.0),
           "32:9 horizontal extent = tan(30) * 32/9");
    }

    /* 6. pointer transforms: window pixel <-> fractions of the safe rect */
    ck(sl_display_rects(2560, 720, 320, 240, SL_ASPECT_32_9, &r), "rects for the pointer case");
    ck(sl_display_to_uv(r.safe, 800, 0, &u, &v) && near(u, 0.0) && near(v, 0.0), "safe top-left -> (0,0)");
    ck(sl_display_to_uv(r.safe, 1280, 360, &u, &v) && near(u, 0.5) && near(v, 0.5), "safe centre -> (0.5,0.5)");
    ck(!sl_display_to_uv(r.safe, 100, 360, &u, &v) && u < 0.0, "a pixel in the left band is outside the safe rect");
    sl_display_from_uv(r.safe, 0.5, 0.5, &px, &py);
    ck(px == 1280 && py == 360, "(0.5,0.5) -> the window centre");
    sl_display_from_uv(r.safe, 1.0, 1.0, &px, &py);
    ck(px == 1760 && py == 720, "(1,1) -> the safe rect's far corner");
    {
        int bad[4] = { 0, 0, 0, 720 };
        ck(!sl_display_to_uv(bad, 1, 1, &u, &v), "degenerate rect -> no pointer");
    }

    /* 6a. the EXTENDED logical position (the widescreen cursor repair): the
     *     physical pointer -> the front end's 440x330 over the safe rect,
     *     confined to the content rect, with the inside / band / outside
     *     answer the hit gate and the drawn cursor both read. */
    {
        float lx = -1.0f, ly = -1.0f;
        int k;
        /* 4:3 at 960x720: content == safe, the identity - the value is the
         * game cursor's own (u * 440 in single precision). */
        ck(sl_display_rects(960, 720, 440, 330, SL_ASPECT_4_3, &r), "rects 960x720 4:3 (440x330 logical)");
        k = sl_display_pointer_logical(r.safe, r.content, 480, 360, 440.0f, 330.0f, &lx, &ly);
        ck(k == 2 && lx == (float) ((float) 480 / (float) 960) * 440.0f && ly == (float) ((float) 360 / (float) 720) * 330.0f,
           "4:3 centre -> inside, exactly u * 440 / v * 330 (220,165)");
        k = sl_display_pointer_logical(r.safe, r.content, 0, 0, 440.0f, 330.0f, &lx, &ly);
        ck(k == 2 && lx == 0.0f && ly == 0.0f, "4:3 top-left pixel -> inside, (0,0)");
        k = sl_display_pointer_logical(r.safe, r.content, 959, 719, 440.0f, 330.0f, &lx, &ly);
        ck(k == 2 && lx > 439.0f && lx <= 440.0f && ly > 329.0f && ly <= 330.0f, "4:3 far pixel -> inside, just under (440,330)");
        k = sl_display_pointer_logical(r.safe, r.content, -5, 900, 440.0f, 330.0f, &lx, &ly);
        ck(k == 0 && lx == 0.0f && ly > 329.0f, "4:3 outside the window -> confined to the content edge, never a hit");
        /* 16:9 at 1280x720: safe 160,0 960x720 - the bands are 160 px, which
         * is 440 * 160 / 960 = 73.33 logical units beyond either edge. */
        ck(sl_display_rects(1280, 720, 440, 330, SL_ASPECT_16_9, &r) && rect_is(r.safe, 160, 0, 960, 720), "rects 1280x720 16:9");
        k = sl_display_pointer_logical(r.safe, r.content, 0, 360, 440.0f, 330.0f, &lx, &ly);
        ck(k == 1 && lx < -73.3f && lx > -73.4f && ly == 165.0f, "16:9 far-left pixel -> the band: drawable at -73.3, not a hit");
        k = sl_display_pointer_logical(r.safe, r.content, 159, 360, 440.0f, 330.0f, &lx, &ly);
        ck(k == 1 && lx < 0.0f, "16:9 the last band pixel (159) -> the band");
        k = sl_display_pointer_logical(r.safe, r.content, 160, 360, 440.0f, 330.0f, &lx, &ly);
        ck(k == 2 && lx == 0.0f, "16:9 the first safe pixel (160) -> inside at 0");
        k = sl_display_pointer_logical(r.safe, r.content, 1279, 360, 440.0f, 330.0f, &lx, &ly);
        ck(k == 1 && lx > 512.0f && lx < 513.5f, "16:9 far-right pixel -> the band at ~512.9");
        k = sl_display_pointer_logical(r.safe, r.content, 640, 360, 440.0f, 330.0f, &lx, &ly);
        ck(k == 2 && lx == 220.0f && ly == 165.0f, "16:9 window centre -> inside (220,165): the 4:3 composition is centred");
        k = sl_display_pointer_logical(r.safe, r.content, 2000, 360, 440.0f, 330.0f, &lx, &ly);
        ck(k == 0 && lx > 512.0f && lx < 513.5f, "16:9 beyond the window -> confined to the right content edge");
        /* 32:9 at 2560x720: safe 800,0 - the band is 800 px = 366.7 logical. */
        ck(sl_display_rects(2560, 720, 440, 330, SL_ASPECT_32_9, &r), "rects 2560x720 32:9");
        k = sl_display_pointer_logical(r.safe, r.content, 0, 0, 440.0f, 330.0f, &lx, &ly);
        ck(k == 1 && lx < -366.6f && lx > -366.7f && ly == 0.0f, "32:9 far-left pixel -> -366.7 (fits the signed 1/4-px tag)");
        /* a pillarboxed fixed framebuffer: 4:3 selected in a 1280x720 window
         * -> content 160,0 960x720 == safe; a pixel in the bar is outside */
        ck(sl_display_rects(1280, 720, 440, 330, SL_ASPECT_4_3, &r) && rect_is(r.content, 160, 0, 960, 720), "rects: 4:3 in a 16:9 window (bars)");
        k = sl_display_pointer_logical(r.safe, r.content, 10, 360, 440.0f, 330.0f, &lx, &ly);
        ck(k == 0 && lx == 0.0f, "a pixel in a pillarbox bar -> outside, confined to the content edge (0)");
        k = sl_display_pointer_logical(r.safe, r.content, 10, 360, 0.0f, 330.0f, &lx, &ly);
        ck(k == -1, "degenerate logical size -> -1");
    }

    /* 6b. the window width the selection asks for at a height (the SDL
     *     backend's rule in windowed mode: height kept, width = height x
     *     aspect - never a strip inside the old window) */
    ck(sl_display_window_width(720, SL_ASPECT_4_3) == 960, "720 tall at 4:3 -> 960 wide (the accepted window)");
    ck(sl_display_window_width(720, SL_ASPECT_16_9) == 1280, "720 tall at 16:9 -> 1280 wide");
    ck(sl_display_window_width(720, SL_ASPECT_32_9) == 2560, "720 tall at 32:9 -> 2560 wide");
    ck(sl_display_window_width(1080, SL_ASPECT_16_9) == 1920 && sl_display_window_width(1080, SL_ASPECT_32_9) == 3840,
       "1080 tall -> 1920 / 3840 (shape, not size)");
    ck(sl_display_window_width(1080, 9) == 1440, "bad id -> 4:3 width");
    ck(sl_display_window_width(0, SL_ASPECT_16_9) == 0, "degenerate height -> 0");
    /* and the rects of the window the rule produces: content = the whole
     * window, the safe rect 4:3 at the full height, centred */
    ck(sl_display_rects(sl_display_window_width(720, SL_ASPECT_32_9), 720, 320, 240, SL_ASPECT_32_9, &r)
       && rect_is(r.content, 0, 0, 2560, 720) && rect_is(r.safe, 800, 0, 960, 720),
       "the 32:9 window: content the whole window, the 2D layer 960x720 (its 4:3 size at that height) centred");

    /* 7. the split-screen gate */
    ck(sl_aspect_active(), "one player: the aspect is active");
    s_test_players = 2;
    ck(!sl_aspect_active(), "two players: the aspect is not active (4:3 layout)");
    s_test_players = 1;

    /* 8. field of view: the store, the conversions, the step, the cap, the
     *    renderer scale and the traversal scales */
    {
        int capped;
        double h;
        sl_aspect_ratio_set(SL_ASPECT_4_3);
        sl_fov_vertical_set(SL_FOV_V_DEFAULT);
        ck(sl_fov_vertical() == 6000, "fov default 6000 = 60.00");
        ck(sl_fov_h16_displayed() == 91, "60.00 vertical displays as h16 91");
        h = sl_fov_h16_from_vertical(60.0);
        ck(h > 91.48 && h < 91.50, "h16(60.00) = 91.49");
        ck(fabs(sl_fov_vertical_from_h16(sl_fov_h16_from_vertical(72.5)) - 72.5) < 1e-9, "vertical <-> h16 round trip");
        ck(fabs(sl_fov_vertical_from_h16(60.0) - 35.98) < 0.01 && fabs(sl_fov_vertical_from_h16(110.0) - 77.56) < 0.01,
           "h16 60 / 110 = vertical 35.98 / 77.56 (the store's 3598..7756)");
        ck(near(sl_display_fov_scale(), 1.0), "default: scale exactly 1.0 (no-op)");
        ck(near(sl_view_scale(), 1.0) && near(sl_view_scale_y(), 1.0), "default 4:3: traversal scales 1, 1");
        sl_fov_step(+1);
        ck(sl_fov_h16_displayed() == 92 && sl_fov_vertical() == 6044, "step +1: h16 92, vertical 60.44");
        sl_fov_step(-1);
        ck(sl_fov_h16_displayed() == 91 && sl_fov_vertical() == 6000, "step -1 back onto 91 snaps to the exact default");
        sl_fov_step(-1);
        ck(sl_fov_h16_displayed() == 90 && sl_fov_vertical() == 5872, "step -1: h16 90, vertical 58.72");
        sl_fov_vertical_set(7756);
        ck(sl_fov_h16_displayed() == 110, "vertical 77.56 displays as 110");
        sl_fov_step(+1);
        ck(sl_fov_h16_displayed() == 110, "step above the max clamps at 110");
        sl_fov_vertical_set(3598);
        sl_fov_step(-1);
        ck(sl_fov_h16_displayed() == 60, "step below the min clamps at 60");
        ck(sl_fov_vertical() == 3598, "the min stays 3598");
        /* the cap: 32:9 at the max exceeds 140 total; 21:9 does not */
        sl_fov_vertical_set(7756);
        h = sl_display_fov_effective(SL_ASPECT_32_9, &capped);
        ck(capped && fabs(h - 75.39) < 0.05, "32:9 at h16 110: total 141.4 > 140 -> capped, effective vertical 75.4");
        h = sl_display_fov_effective(SL_ASPECT_21_9, &capped);
        ck(!capped && fabs(h - 77.56) < 0.01, "21:9 at h16 110: total 123.9, no cap");
        h = sl_display_fov_effective(SL_ASPECT_16_9, &capped);
        ck(!capped, "16:9 at h16 110: total 110, no cap");
        sl_fov_vertical_set(SL_FOV_V_DEFAULT);
        h = sl_display_fov_effective(SL_ASPECT_32_9, &capped);
        ck(!capped && near(h, 60.0), "32:9 at the default: total 128.1, no cap, vertical 60");
        /* the renderer scale and the traversal scales at a wider setting */
        sl_fov_vertical_set(7756);
        sl_aspect_ratio_set(SL_ASPECT_16_9);
        ck(fabs(sl_display_fov_scale() - tan(SL_DEG2RAD(30.0)) / tan(SL_DEG2RAD(77.56) * 0.5)) < 1e-6,
           "16:9 at h16 110: scale tan(30)/tan(38.78) = 0.719");
        ck(fabs(sl_view_scale() - (4.0 / 3.0) / sl_display_fov_scale()) < 1e-5
           && fabs(sl_view_scale_y() - 1.0 / sl_display_fov_scale()) < 1e-5,
           "traversal scales k/s and 1/s");
        s_test_in_level = 0;
        ck(near(sl_display_fov_scale(), 1.0) && near(sl_view_scale_y(), 1.0), "no level (the front end): scale 1");
        s_test_in_level = 1;
        s_test_players = 2;
        ck(near(sl_display_fov_scale(), 1.0), "split-screen: scale 1");
        s_test_players = 1;
        sl_fov_vertical_set(SL_FOV_V_DEFAULT);
        sl_aspect_ratio_set(SL_ASPECT_4_3);
    }

    /* 9. the PC display modes (#52): sl_window.c's own checks over the same
     *    active store - the lists, the fallbacks, the request / take /
     *    commit seam and the editors' steps (sl_window_selftest). */
    {
        extern void sl_window_selftest(int *checks, int *fails);
        int wc = 0, wf = 0;
        sl_window_selftest(&wc, &wf);
        g_checks += wc;
        g_fail += wf;
        printf("sl_window selftest: %d checks, %d failed\n", wc, wf);
    }

    remove(path);
    printf("sl_display selftest: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
#endif /* SL_DISPLAY_SELFTEST */
