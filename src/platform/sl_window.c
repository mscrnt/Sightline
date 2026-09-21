/**
 * sl_window.c - the PC display modes (#52). See sl_window.h.
 *
 * WHY src/platform. Pure host arithmetic over the settings store and a few
 * ints the SDL backend publishes: no SDL, no GL, no game type - the same
 * class as sl_settings.c and sl_display.c, so src/gfx and the self-test
 * (tools/windows/displaytest.ps1, -DSL_DISPLAY_SELFTEST beside
 * sl_display.c) can both include the header and the arithmetic runs without
 * a window.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sl_settings.h"
#include "sl_display.h"
#include "sl_window.h"

/* ---- the store, typed --------------------------------------------------- */

int sl_window_mode(void)
{
    int m = sl_settings_get(SL_SET_WINDOW_MODE);
    if (m < 0 || m >= SL_WINDOW_MODE_COUNT)
        return SL_WINDOW_WINDOWED;
    return m;
}

const char *sl_window_mode_name(int mode)
{
    static const char *const names[SL_WINDOW_MODE_COUNT] = { "WINDOWED", "BORDERLESS", "FULLSCREEN" };
    return (mode >= 0 && mode < SL_WINDOW_MODE_COUNT) ? names[mode] : "?";
}

static int stored_pair(int id_w, int id_h, int *w, int *h)
{
    int sw = sl_settings_get(id_w), sh = sl_settings_get(id_h);
    if (sw <= 0 || sh <= 0 || sw > SL_WINDOW_SIZE_MAX || sh > SL_WINDOW_SIZE_MAX) {
        if (w) *w = 0;
        if (h) *h = 0;
        return 0;
    }
    if (w) *w = sw;
    if (h) *h = sh;
    return 1;
}

int sl_window_stored_windowed(int *w, int *h)   { return stored_pair(SL_SET_WINDOW_WIDTH, SL_SET_WINDOW_HEIGHT, w, h); }
int sl_window_stored_fullscreen(int *w, int *h) { return stored_pair(SL_SET_FULLSCREEN_WIDTH, SL_SET_FULLSCREEN_HEIGHT, w, h); }

int sl_vsync(void)
{
    return sl_settings_get(SL_SET_VSYNC) == 1;
}

/* ---- the lists: pure ---------------------------------------------------- */

static void list_insert_sorted(struct sl_window_list *out, int w, int h)
{
    int i, j;
    /* ascending by width, then height; duplicates dropped */
    for (i = 0; i < out->n; i++) {
        if (out->w[i] == w && out->h[i] == h) return;
        if (out->w[i] > w || (out->w[i] == w && out->h[i] > h)) break;
    }
    if (out->n >= SL_WINDOW_LIST_MAX) {
        /* full: keep the LARGEST entries - drop the smallest to make room,
         * unless this one is smaller than everything held */
        if (i == 0) return;
        for (j = 0; j + 1 < i; j++) { out->w[j] = out->w[j + 1]; out->h[j] = out->h[j + 1]; }
        i--;
        out->n--;
    }
    for (j = out->n; j > i; j--) { out->w[j] = out->w[j - 1]; out->h[j] = out->h[j - 1]; }
    out->w[i] = w; out->h[i] = h;
    out->n++;
}

void sl_window_list_modes(const int *mw, const int *mh, int n, struct sl_window_list *out)
{
    int i;
    out->n = 0;
    if (mw == NULL || mh == NULL) return;
    for (i = 0; i < n; i++) {
        if (mw[i] < SL_WINDOW_MIN_W || mh[i] < SL_WINDOW_MIN_H) continue;
        if (mw[i] > SL_WINDOW_SIZE_MAX || mh[i] > SL_WINDOW_SIZE_MAX) continue;
        list_insert_sorted(out, mw[i], mh[i]);
    }
}

void sl_window_list_windowed(const struct sl_window_list *modes, int desk_w, int desk_h, int aspect,
                             struct sl_window_list *out)
{
    int i, j;
    int heights[SL_WINDOW_LIST_MAX];
    int nh = 0;
    out->n = 0;
    if (modes == NULL || desk_w <= 0 || desk_h <= 0) return;
    /* the distinct heights, ascending */
    for (i = 0; i < modes->n; i++) {
        int h = modes->h[i];
        for (j = 0; j < nh; j++) if (heights[j] == h) break;
        if (j < nh) continue;
        for (j = nh; j > 0 && heights[j - 1] > h; j--) heights[j] = heights[j - 1];
        heights[j] = h;
        nh++;
    }
    for (i = 0; i < nh; i++) {
        int h = heights[i];
        int w = sl_display_window_width(h, aspect);
        if (h > desk_h || w > desk_w || w <= 0) continue;
        out->w[out->n] = w; out->h[out->n] = h;
        out->n++;
    }
}

int sl_window_list_find(const struct sl_window_list *l, int w, int h)
{
    int i;
    if (l == NULL) return -1;
    for (i = 0; i < l->n; i++)
        if (l->w[i] == w && l->h[i] == h) return i;
    return -1;
}

/* Ordering for the step: by height, then width (the windowed list is by
 * height; the fullscreen list's width order agrees with it except across
 * aspects, where the taller mode is the "next" one a player expects). */
static int size_less(int w1, int h1, int w2, int h2)
{
    return h1 < h2 || (h1 == h2 && w1 < w2);
}

int sl_window_list_step(const struct sl_window_list *l, int w, int h, int dir)
{
    int i, best = -1;
    if (l == NULL || l->n <= 0) return -1;
    if (dir > 0) {
        for (i = 0; i < l->n; i++)
            if (size_less(w, h, l->w[i], l->h[i]) && (best < 0 || size_less(l->w[i], l->h[i], l->w[best], l->h[best])))
                best = i;
        if (best < 0) {                         /* nothing larger: the largest */
            best = 0;
            for (i = 1; i < l->n; i++) if (size_less(l->w[best], l->h[best], l->w[i], l->h[i])) best = i;
        }
    } else {
        for (i = 0; i < l->n; i++)
            if (size_less(l->w[i], l->h[i], w, h) && (best < 0 || size_less(l->w[best], l->h[best], l->w[i], l->h[i])))
                best = i;
        if (best < 0) {                         /* nothing smaller: the smallest */
            best = 0;
            for (i = 1; i < l->n; i++) if (size_less(l->w[i], l->h[i], l->w[best], l->h[best])) best = i;
        }
    }
    return best;
}

/* ---- the fallbacks: pure ------------------------------------------------ */

int sl_window_pick_fullscreen(const struct sl_window_list *modes, int want_w, int want_h,
                              int desk_w, int desk_h, int *w, int *h)
{
    if (want_w > 0 && want_h > 0 && sl_window_list_find(modes, want_w, want_h) >= 0) {
        *w = want_w; *h = want_h;
        return 1;
    }
    *w = desk_w; *h = desk_h;
    return 0;
}

int sl_window_pick_windowed(const struct sl_window_list *wlist, int want_h, int launch_h, int aspect,
                            int *w, int *h)
{
    int i, used = 0;
    int th = launch_h;
    if (want_h > 0 && wlist != NULL) {
        for (i = 0; i < wlist->n; i++)
            if (wlist->h[i] == want_h) { th = want_h; used = 1; break; }
    }
    if (th <= 0) th = SL_WINDOW_MIN_H;
    *h = th;
    *w = sl_display_window_width(th, aspect);
    return used;
}

/* ---- what the backend publishes ---------------------------------------- */

static struct sl_window_list s_modes;          /* the display's fullscreen list */
static struct sl_window_list s_windowed;       /* derived on demand, at the current aspect */
static int s_have_lists;
static int s_desk_w, s_desk_h;
static int s_have_state;
static int s_mode, s_w, s_h, s_vsync;

void sl_window_publish_lists(const struct sl_window_list *modes, int desk_w, int desk_h)
{
    if (modes != NULL) s_modes = *modes; else s_modes.n = 0;
    s_desk_w = desk_w; s_desk_h = desk_h;
    s_have_lists = 1;
}

void sl_window_publish_state(int mode, int w, int h, int vsync)
{
    s_mode = mode; s_w = w; s_h = h; s_vsync = vsync ? 1 : 0;
    s_have_state = 1;
}

int sl_window_state(int *mode, int *w, int *h, int *vsync)
{
    if (mode) *mode = s_mode;
    if (w) *w = s_w;
    if (h) *h = s_h;
    if (vsync) *vsync = s_vsync;
    return s_have_state;
}

int sl_window_desktop(int *w, int *h)
{
    if (w) *w = s_desk_w;
    if (h) *h = s_desk_h;
    return s_have_lists && s_desk_w > 0 && s_desk_h > 0;
}

const struct sl_window_list *sl_window_list_fullscreen(void)
{
    return &s_modes;
}

const struct sl_window_list *sl_window_list_windowed_now(void)
{
    sl_window_list_windowed(&s_modes, s_desk_w, s_desk_h, sl_aspect_ratio(), &s_windowed);
    return &s_windowed;
}

/* ---- requests ----------------------------------------------------------- */

static int s_req_mask;
static int s_req_mode, s_req_w, s_req_h, s_req_vsync;

void sl_window_request_mode(int mode)
{
    if (mode < 0 || mode >= SL_WINDOW_MODE_COUNT) return;
    s_req_mode = mode;
    s_req_mask |= SL_WINDOW_REQ_MODE;
}

void sl_window_request_size(int w, int h)
{
    if (w <= 0 || h <= 0 || w > SL_WINDOW_SIZE_MAX || h > SL_WINDOW_SIZE_MAX) return;
    if (sl_window_mode_shown() == SL_WINDOW_BORDERLESS && !(s_req_mask & SL_WINDOW_REQ_MODE)) return;
    s_req_w = w; s_req_h = h;
    s_req_mask |= SL_WINDOW_REQ_SIZE;
}

void sl_window_request_vsync(int on)
{
    s_req_vsync = on ? 1 : 0;
    s_req_mask |= SL_WINDOW_REQ_VSYNC;
}

int sl_window_request_take(int *mode, int *w, int *h, int *vsync)
{
    int m = s_req_mask;
    if (mode) *mode = s_req_mode;
    if (w) *w = s_req_w;
    if (h) *h = s_req_h;
    if (vsync) *vsync = s_req_vsync;
    s_req_mask = 0;
    return m;
}

int sl_window_request_pending(void)
{
    return s_req_mask;
}

void sl_window_commit(int mode, int size_chosen, int w, int h, int vsync_chosen, int vsync)
{
    sl_settings_batch_begin();
    if (mode >= 0 && mode < SL_WINDOW_MODE_COUNT)
        sl_settings_set(SL_SET_WINDOW_MODE, mode);
    if (size_chosen && w > 0 && h > 0) {
        if (mode == SL_WINDOW_WINDOWED) {
            sl_settings_set(SL_SET_WINDOW_WIDTH, w);
            sl_settings_set(SL_SET_WINDOW_HEIGHT, h);
        } else if (mode == SL_WINDOW_FULLSCREEN) {
            sl_settings_set(SL_SET_FULLSCREEN_WIDTH, w);
            sl_settings_set(SL_SET_FULLSCREEN_HEIGHT, h);
        }
    }
    if (vsync_chosen)
        sl_settings_set(SL_SET_VSYNC, vsync ? 1 : 0);
    sl_settings_batch_end();
}

/* ---- the editors' steps ------------------------------------------------- */

int sl_window_mode_shown(void)
{
    return s_have_state ? s_mode : sl_window_mode();
}

void sl_window_mode_step(int dir)
{
    int m = sl_window_mode_shown();
    if (dir == 0) return;
    m += dir > 0 ? 1 : -1;
    if (m < 0) m = SL_WINDOW_MODE_COUNT - 1;
    if (m >= SL_WINDOW_MODE_COUNT) m = 0;
    sl_window_request_mode(m);
}

int sl_window_size_shown(int *w, int *h)
{
    if (!s_have_state) { if (w) *w = 0; if (h) *h = 0; return 0; }
    if (s_mode == SL_WINDOW_BORDERLESS && s_desk_w > 0 && s_desk_h > 0) {
        if (w) *w = s_desk_w;
        if (h) *h = s_desk_h;
        return 1;
    }
    if (w) *w = s_w;
    if (h) *h = s_h;
    return s_w > 0 && s_h > 0;
}

int sl_window_size_editable(void)
{
    int m = sl_window_mode_shown();
    if (!s_have_lists || !s_have_state) return 0;
    if (m == SL_WINDOW_BORDERLESS) return 0;
    if (m == SL_WINDOW_FULLSCREEN) return s_modes.n > 0;
    return sl_window_list_windowed_now()->n > 0;
}

void sl_window_size_step(int dir)
{
    const struct sl_window_list *l;
    int m = sl_window_mode_shown();
    int w, h, i;
    if (dir == 0 || !sl_window_size_editable()) return;
    if (!sl_window_size_shown(&w, &h)) return;
    l = (m == SL_WINDOW_FULLSCREEN) ? &s_modes : sl_window_list_windowed_now();
    i = sl_window_list_step(l, w, h, dir);
    if (i < 0) return;
    if (l->w[i] == w && l->h[i] == h) return;     /* already at the end */
    sl_window_request_size(l->w[i], l->h[i]);
}

void sl_window_vsync_toggle(void)
{
    int v = s_have_state ? s_vsync : sl_vsync();
    sl_window_request_vsync(!v);
}

void sl_window_size_text(int w, int h, char *buf, int n)
{
    /* by hand: the decomp's sprintf is not libc's (sl_gfx_sdl.c sdl_shot) */
    char tmp[24];
    int i = 0, k, d, started;
    if (buf == NULL || n <= 0) return;
    for (k = 0; k < 2; k++) {
        int v = k == 0 ? w : h;
        if (v < 0) v = 0;
        started = 0;
        for (d = 10000; d > 0; d /= 10) {
            int c = (v / d) % 10;
            if (c != 0 || started || d == 1) { tmp[i++] = (char) ('0' + c); started = 1; }
        }
        if (k == 0) tmp[i++] = 'x';
    }
    tmp[i] = '\0';
    if (i >= n) i = n - 1;
    memcpy(buf, tmp, (size_t) i);
    buf[i] = '\0';
}

/* ---- THE QUIT REQUEST ---------------------------------------------------
 *
 * Owner-observed 2026-09-20: the dossier's main menu had no QUIT GAME, so at
 * a large resolution or in fullscreen there was no normal way to close the
 * game. The front end's QUIT GAME row files this ONE flag and the frame pump
 * (src/platform/sl_ultra_shim.c) honours it at the next frame boundary,
 * where it takes the very path closing the window takes: the backend torn
 * down - the pointer grab dropped, the GL context and the window destroyed,
 * SDL_Quit restoring any exclusive mode - then exit(0) with its atexit work
 * (the save flush, the reports). NOTHING exits from menu code: a tree-wide
 * `grep -rn "exit(\|_exit(\|abort(\|ExitProcess" src/game src/native` finds
 * two hits and neither is a call - a declaration in
 * initBondDATAdefaults.h:7 and the word inside a comment at model.c:243.
 *
 * It lives here, with the window's other lifecycle state, rather than in the
 * pump: this file is host-clean and already linked into the display
 * self-test, so "the row's activation files the request, and only that"
 * is asserted by a gate instead of only by a run. Latching - a second
 * request is not a second quit - because a held confirm must not be able to
 * race the pump's single test. */
static int g_quit_req;

void sl_quit_request(void)
{
    if (!g_quit_req)
        fprintf(stderr, "sightline native: QUIT GAME requested (the frame pump exits at the next frame boundary)\n");
    g_quit_req = 1;
}

int sl_quit_requested(void)
{
    return g_quit_req;
}

#ifdef SL_DISPLAY_SELFTEST
void sl_quit_request_clear_for_test(void)
{
    g_quit_req = 0;
}
#endif

/* ====================================================================== */
#ifdef SL_DISPLAY_SELFTEST

static int g_wchecks, g_wfail;
static void wck(int cond, const char *what)
{
    g_wchecks++;
    if (!cond) { g_wfail++; printf("  FAIL  %s\n", what); }
}

void sl_window_selftest(int *checks, int *fails)
{
    struct sl_window_list modes, wl;
    /* the owner's display as SDL 2.32.10 lists it (95 modes over 15 sizes,
     * measured 2026-09-20), plus a portrait mode and two undersized ones */
    static const int mw[] = { 5120, 5120, 3840, 3840, 3840, 2560, 2560, 2560, 2560, 1920, 1920, 1920, 1680, 1600, 1440,
                              1280, 1280, 1280, 1152, 1024, 1024, 800, 800, 640, 640, 480, 320, 720 };
    static const int mh[] = { 1440, 1440, 1080, 1080, 1080, 1440, 1440, 1080, 1080, 1080, 1080, 1080, 1050, 900, 900,
                              1024, 800, 720, 864, 768, 768, 600, 600, 480, 480, 640, 240, 1280 };
    int n = (int) (sizeof mw / sizeof mw[0]);
    int w, h, i, m, v;
    char txt[16];

    /* 1. names and the store's typed reads */
    wck(strcmp(sl_window_mode_name(SL_WINDOW_WINDOWED), "WINDOWED") == 0 && strcmp(sl_window_mode_name(SL_WINDOW_BORDERLESS), "BORDERLESS") == 0
        && strcmp(sl_window_mode_name(SL_WINDOW_FULLSCREEN), "FULLSCREEN") == 0 && strcmp(sl_window_mode_name(3), "?") == 0,
        "mode names WINDOWED / BORDERLESS / FULLSCREEN, ? out of range");
    wck(sl_window_mode() == SL_WINDOW_WINDOWED && !sl_window_stored_windowed(&w, &h) && w == 0 && h == 0
        && !sl_window_stored_fullscreen(&w, &h) && !sl_vsync(),
        "defaults: WINDOWED, no windowed pair, no fullscreen pair, vsync off");

    /* 2. the fullscreen list: deduped by w x h, ascending, undersized and
     *    portrait-below-minimum dropped */
    sl_window_list_modes(mw, mh, n, &modes);
    wck(modes.n == 16, "16 distinct sizes from 28 modes (dupes dropped, 480x640 / 320x240 dropped, 720x1280 kept)");
    wck(modes.w[0] == 640 && modes.h[0] == 480 && modes.w[modes.n - 1] == 5120 && modes.h[modes.n - 1] == 1440,
        "ascending: 640x480 first, 5120x1440 last");
    wck(sl_window_list_find(&modes, 1920, 1080) >= 0 && sl_window_list_find(&modes, 1280, 720) >= 0
        && sl_window_list_find(&modes, 9999, 7777) < 0 && sl_window_list_find(&modes, 960, 720) < 0,
        "find: 1920x1080 and 1280x720 present, 9999x7777 and 960x720 absent");
    for (i = 1, m = 1; i < modes.n; i++)
        if (!(modes.w[i - 1] < modes.w[i] || (modes.w[i - 1] == modes.w[i] && modes.h[i - 1] < modes.h[i]))) m = 0;
    wck(m, "strictly ascending by width then height (no duplicates)");
    wck(sl_window_list_find(&modes, 2560, 1440) >= 0 && sl_window_list_find(&modes, 2560, 1080) >= 0
        && sl_window_list_find(&modes, 2560, 1080) < sl_window_list_find(&modes, 2560, 1440),
        "2560x1080 before 2560x1440");
    sl_window_list_modes(NULL, NULL, 0, &wl);
    wck(wl.n == 0, "no modes -> empty list");
    {   /* the bound: 40 distinct sizes keep the 32 largest */
        int bw[40], bh[40];
        for (i = 0; i < 40; i++) { bw[i] = 640 + i * 16; bh[i] = 480 + i * 9; }
        sl_window_list_modes(bw, bh, 40, &wl);
        wck(wl.n == SL_WINDOW_LIST_MAX && wl.w[0] == 640 + 8 * 16 && wl.w[wl.n - 1] == 640 + 39 * 16,
            "40 sizes -> the 32 largest kept, ascending");
    }

    /* 3. the windowed list at the owner's 5120x1440 desktop: the distinct
     *    heights that fit, as (height x aspect) x height */
    sl_window_list_windowed(&modes, 5120, 1440, SL_ASPECT_4_3, &wl);
    wck(wl.n == 12 && wl.h[0] == 480 && wl.w[0] == 640 && wl.h[2] == 720 && wl.w[2] == 960 && wl.h[11] == 1440 && wl.w[11] == 1920
        && wl.h[10] == 1280 && wl.w[10] == 1707,
        "4:3 at 5120x1440: 12 heights 480..1440 (the portrait mode's 1280 among them), 640x480 / 960x720 / 1707x1280 / 1920x1440");
    sl_window_list_windowed(&modes, 5120, 1440, SL_ASPECT_16_9, &wl);
    wck(wl.n == 12 && sl_window_list_find(&wl, 1280, 720) >= 0 && sl_window_list_find(&wl, 1920, 1080) >= 0 && sl_window_list_find(&wl, 2560, 1440) >= 0,
        "16:9 at 5120x1440: 1280x720, 1920x1080, 2560x1440 offered");
    sl_window_list_windowed(&modes, 5120, 1440, SL_ASPECT_32_9, &wl);
    wck(wl.n == 12 && sl_window_list_find(&wl, 2560, 720) >= 0 && sl_window_list_find(&wl, 5120, 1440) >= 0,
        "32:9 at 5120x1440: 2560x720 .. 5120x1440 all fit");
    sl_window_list_windowed(&modes, 1920, 1080, SL_ASPECT_32_9, &wl);
    wck(wl.n == 1 && wl.h[0] == 480 && wl.w[0] == 1707,
        "32:9 at a 1920x1080 desktop: only the height whose 32:9 width fits (480 -> 1707 wide; 600 -> 2133 does not)");
    sl_window_list_windowed(&modes, 1920, 1080, SL_ASPECT_16_9, &wl);
    wck(wl.n == 10 && wl.h[wl.n - 1] == 1080 && wl.w[wl.n - 1] == 1920 && sl_window_list_find(&wl, 2560, 1440) < 0,
        "16:9 at a 1920x1080 desktop: ten heights up to 1920x1080, 1280 and 1440 tall dropped");
    sl_window_list_windowed(&modes, 0, 0, SL_ASPECT_4_3, &wl);
    wck(wl.n == 0, "no desktop -> empty windowed list");

    /* 4. the step from on- and off-list sizes */
    sl_window_list_windowed(&modes, 5120, 1440, SL_ASPECT_16_9, &wl);
    i = sl_window_list_step(&wl, 1280, 720, +1);
    wck(i >= 0 && wl.h[i] == 768, "step up from 1280x720 -> the 768-tall entry");
    i = sl_window_list_step(&wl, 1280, 720, -1);
    wck(i >= 0 && wl.h[i] == 600, "step down from 1280x720 -> the 600-tall entry");
    i = sl_window_list_step(&wl, 1000, 700, +1);
    wck(i >= 0 && wl.h[i] == 720, "step up from an off-list 700 tall -> 720");
    i = sl_window_list_step(&wl, 1000, 700, -1);
    wck(i >= 0 && wl.h[i] == 600, "step down from an off-list 700 tall -> 600");
    i = sl_window_list_step(&wl, 2560, 1440, +1);
    wck(i >= 0 && wl.h[i] == 1440, "step up at the top stays at the top");
    i = sl_window_list_step(&wl, 640, 360, -1);
    wck(i >= 0 && wl.h[i] == 480, "step down below the bottom lands on the bottom");
    i = sl_window_list_step(&modes, 2560, 1080, +1);
    wck(i >= 0 && modes.w[i] == 3840 && modes.h[i] == 1080, "fullscreen step up from 2560x1080 -> 3840x1080 (by height, then width)");
    i = sl_window_list_step(&modes, 3840, 1080, +1);
    wck(i >= 0 && modes.w[i] == 720 && modes.h[i] == 1280, "fullscreen step up from 3840x1080 -> the portrait 720x1280 (the next height; real display data)");
    i = sl_window_list_step(&modes, 1920, 1080, +1);
    wck(i >= 0 && modes.w[i] == 2560 && modes.h[i] == 1080, "fullscreen step up from 1920x1080 -> 2560x1080");
    wck(sl_window_list_step(&wl, 1, 1, +1) >= 0 && sl_window_list_step(NULL, 1, 1, +1) < 0, "empty / NULL list -> -1");

    /* 5. the fallbacks */
    wck(sl_window_pick_fullscreen(&modes, 1920, 1080, 5120, 1440, &w, &h) == 1 && w == 1920 && h == 1080, "fullscreen: an offered pair is used");
    wck(sl_window_pick_fullscreen(&modes, 9999, 7777, 5120, 1440, &w, &h) == 0 && w == 5120 && h == 1440, "fullscreen: 9999x7777 -> the desktop mode");
    wck(sl_window_pick_fullscreen(&modes, 0, 0, 5120, 1440, &w, &h) == 0 && w == 5120 && h == 1440, "fullscreen: nothing chosen -> the desktop mode");
    wck(sl_window_pick_fullscreen(&modes, 960, 720, 5120, 1440, &w, &h) == 0 && w == 5120 && h == 1440, "fullscreen: the 4:3 window's 960x720 is not a mode -> the desktop mode");
    sl_window_list_windowed(&modes, 5120, 1440, SL_ASPECT_16_9, &wl);
    wck(sl_window_pick_windowed(&wl, 1080, 720, SL_ASPECT_16_9, &w, &h) == 1 && w == 1920 && h == 1080, "windowed: a stored 1080 tall -> 1920x1080 at 16:9");
    wck(sl_window_pick_windowed(&wl, 1080, 720, SL_ASPECT_4_3, &w, &h) == 1 && w == 1440 && h == 1080, "windowed: the same height at 4:3 -> 1440x1080 (the aspect wins on the width)");
    wck(sl_window_pick_windowed(&wl, 7777, 720, SL_ASPECT_16_9, &w, &h) == 0 && w == 1280 && h == 720, "windowed: a stored 7777 tall -> the launcher's 720 (1280x720 at 16:9)");
    wck(sl_window_pick_windowed(&wl, 0, 720, SL_ASPECT_4_3, &w, &h) == 0 && w == 960 && h == 720, "windowed: nothing chosen -> the launcher's 960x720 at 4:3 (the accepted launch)");
    wck(sl_window_pick_windowed(&wl, 0, 0, SL_ASPECT_4_3, &w, &h) == 0 && h == 480 && w == 640, "windowed: no launcher height either -> 640x480 (the compiled default)");

    /* 6. requests, take, the state and the steps over the published lists */
    wck(!sl_window_state(&m, &w, &h, &v) && !sl_window_size_shown(&w, &h) && !sl_window_size_editable(),
        "nothing published: no state, no size shown, the row not editable");
    wck(sl_window_mode_shown() == SL_WINDOW_WINDOWED, "mode shown = the store's WINDOWED before a publish");
    sl_window_publish_lists(&modes, 5120, 1440);
    sl_window_publish_state(SL_WINDOW_WINDOWED, 1280, 720, 0);
    wck(sl_window_state(&m, &w, &h, &v) && m == SL_WINDOW_WINDOWED && w == 1280 && h == 720 && v == 0
        && sl_window_size_shown(&w, &h) && w == 1280 && h == 720 && sl_window_size_editable() && sl_window_desktop(&w, &h) && w == 5120 && h == 1440,
        "published WINDOWED 1280x720 vsync off: state, size shown, editable, desktop 5120x1440");
    wck(sl_window_request_take(&m, &w, &h, &v) == 0, "no request pending");
    sl_window_mode_step(+1);
    wck(sl_window_request_pending() == SL_WINDOW_REQ_MODE && sl_window_request_take(&m, &w, &h, &v) == SL_WINDOW_REQ_MODE && m == SL_WINDOW_BORDERLESS
        && sl_window_request_take(&m, &w, &h, &v) == 0, "mode step +1 from WINDOWED requests BORDERLESS; the take clears it");
    sl_window_mode_step(-1);
    wck(sl_window_request_take(&m, &w, &h, &v) == SL_WINDOW_REQ_MODE && m == SL_WINDOW_FULLSCREEN, "mode step -1 from WINDOWED wraps to FULLSCREEN");
    sl_aspect_ratio_set(SL_ASPECT_16_9);
    sl_window_size_step(+1);
    wck(sl_window_request_take(&m, &w, &h, &v) == SL_WINDOW_REQ_SIZE && h == 768 && w == sl_display_window_width(768, SL_ASPECT_16_9),
        "size step +1 in WINDOWED at 16:9 from 1280x720 requests the 768-tall entry at its 16:9 width");
    sl_window_size_step(-1);
    wck(sl_window_request_take(&m, &w, &h, &v) == SL_WINDOW_REQ_SIZE && h == 600, "size step -1 requests 600 tall");
    sl_aspect_ratio_set(SL_ASPECT_4_3);
    sl_window_vsync_toggle();
    wck(sl_window_request_take(&m, &w, &h, &v) == SL_WINDOW_REQ_VSYNC && v == 1, "vsync toggle from off requests on");
    sl_window_request_mode(3);
    sl_window_request_mode(-1);
    sl_window_request_size(0, 720);
    sl_window_request_size(20000, 720);
    wck(sl_window_request_take(&m, &w, &h, &v) == 0, "out-of-range mode / size requests refused");
    sl_window_publish_state(SL_WINDOW_BORDERLESS, 5120, 1440, 0);
    wck(sl_window_size_shown(&w, &h) && w == 5120 && h == 1440 && !sl_window_size_editable(), "BORDERLESS: the desktop size shown, the row informational");
    sl_window_size_step(+1);
    sl_window_request_size(1280, 720);
    wck(sl_window_request_take(&m, &w, &h, &v) == 0, "BORDERLESS: a size step / request is ignored");
    sl_window_publish_state(SL_WINDOW_FULLSCREEN, 1920, 1080, 1);
    wck(sl_window_size_shown(&w, &h) && w == 1920 && h == 1080 && sl_window_size_editable(), "FULLSCREEN 1920x1080: the mode shown, editable");
    sl_window_size_step(+1);
    wck(sl_window_request_take(&m, &w, &h, &v) == SL_WINDOW_REQ_SIZE && w == 2560 && h == 1080, "FULLSCREEN step +1 from 1920x1080 requests 2560x1080");
    sl_window_size_step(-1);
    wck(sl_window_request_take(&m, &w, &h, &v) == SL_WINDOW_REQ_SIZE && w == 1680 && h == 1050, "FULLSCREEN step -1 from 1920x1080 requests 1680x1050");
    sl_window_vsync_toggle();
    wck(sl_window_request_take(&m, &w, &h, &v) == SL_WINDOW_REQ_VSYNC && v == 0, "vsync toggle from the published on requests off");
    sl_window_mode_step(+1);
    sl_window_size_step(+1);
    sl_window_vsync_toggle();
    wck(sl_window_request_take(&m, &w, &h, &v) == (SL_WINDOW_REQ_MODE | SL_WINDOW_REQ_SIZE | SL_WINDOW_REQ_VSYNC) && m == SL_WINDOW_WINDOWED,
        "three requests in one frame: one take carries all three (FULLSCREEN +1 wraps to WINDOWED)");

    /* 7. the commit: what SDL confirmed lands in the store, a fallback never */
    sl_window_commit(SL_WINDOW_WINDOWED, 1, 1280, 720, 0, 0);
    wck(sl_window_mode() == SL_WINDOW_WINDOWED && sl_window_stored_windowed(&w, &h) && w == 1280 && h == 720 && !sl_window_stored_fullscreen(&w, &h) && !sl_vsync(),
        "commit WINDOWED 1280x720 (size chosen): window_* written, fullscreen_* and vsync untouched");
    sl_window_commit(SL_WINDOW_FULLSCREEN, 0, 5120, 1440, 0, 0);
    wck(sl_window_mode() == SL_WINDOW_FULLSCREEN && !sl_window_stored_fullscreen(&w, &h) && sl_window_stored_windowed(&w, &h) && w == 1280,
        "commit FULLSCREEN with a FALLBACK size (not chosen): the mode written, fullscreen_* stays unchosen, window_* kept");
    sl_window_commit(SL_WINDOW_FULLSCREEN, 1, 1920, 1080, 1, 1);
    wck(sl_window_stored_fullscreen(&w, &h) && w == 1920 && h == 1080 && sl_vsync() && sl_window_stored_windowed(&w, &h) && w == 1280 && h == 720,
        "commit FULLSCREEN 1920x1080 vsync on (both chosen): fullscreen_* and vsync written, window_* kept");
    sl_window_commit(SL_WINDOW_BORDERLESS, 1, 5120, 1440, 0, 0);
    wck(sl_window_mode() == SL_WINDOW_BORDERLESS && sl_window_stored_windowed(&w, &h) && w == 1280 && sl_window_stored_fullscreen(&w, &h) && w == 1920,
        "commit BORDERLESS: no size row is written even when one was chosen (the desktop owns it)");
    sl_window_commit(SL_WINDOW_WINDOWED, 0, 0, 0, 1, 0);
    wck(sl_window_mode() == SL_WINDOW_WINDOWED && !sl_vsync() && sl_window_stored_windowed(&w, &h) && h == 720, "commit WINDOWED, vsync off chosen: both written, the pair kept");
    sl_window_commit(9, 1, 100, 100, 0, 0);
    wck(sl_window_mode() == SL_WINDOW_WINDOWED && sl_window_stored_windowed(&w, &h) && w == 1280, "commit with a bad mode: nothing written");

    /* 8. the text */
    sl_window_size_text(1280, 720, txt, sizeof txt);
    wck(strcmp(txt, "1280x720") == 0, "text 1280x720");
    sl_window_size_text(640, 480, txt, sizeof txt);
    wck(strcmp(txt, "640x480") == 0, "text 640x480");
    sl_window_size_text(5120, 1440, txt, sizeof txt);
    wck(strcmp(txt, "5120x1440") == 0, "text 5120x1440");
    sl_window_size_text(0, 0, txt, sizeof txt);
    wck(strcmp(txt, "0x0") == 0, "text 0x0");
    sl_window_size_text(1920, 1080, txt, 6);
    wck(strcmp(txt, "1920x") == 0, "text truncated to the buffer");

    /* 9. the QUIT request (the front end's QUIT GAME row): unset until it is
     *    filed, latching, and touching nothing else - the pump's exit is the
     *    only consumer and is never reached from a test. */
    sl_quit_request_clear_for_test();
    wck(!sl_quit_requested(), "quit: not requested until the row is activated");
    {
        int m0 = sl_window_mode(), pend0 = sl_window_request_pending();
        sl_quit_request();
        wck(sl_quit_requested() == 1, "quit: the row's activation files the request");
        sl_quit_request();
        wck(sl_quit_requested() == 1, "quit: a second activation is not a second quit (latched)");
        wck(sl_window_mode() == m0 && sl_window_request_pending() == pend0,
            "quit: the request changes no display state and files no display request");
    }
    sl_quit_request_clear_for_test();
    wck(!sl_quit_requested(), "quit: cleared for the rest of the fixture");

    /* leave the store as the display test expects it */
    sl_window_commit(SL_WINDOW_WINDOWED, 0, 0, 0, 1, 0);
    sl_settings_set(SL_SET_WINDOW_WIDTH, 0);
    sl_settings_set(SL_SET_WINDOW_HEIGHT, 0);
    sl_settings_set(SL_SET_FULLSCREEN_WIDTH, 0);
    sl_settings_set(SL_SET_FULLSCREEN_HEIGHT, 0);
    *checks += g_wchecks;
    *fails += g_wfail;
}
#endif /* SL_DISPLAY_SELFTEST */
