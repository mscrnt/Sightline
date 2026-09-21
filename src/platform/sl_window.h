/**
 * sl_window.h - the PC display modes (#52): WINDOW MODE, RESOLUTION and
 * VSYNC as the player's persisted choice, the lists the display offers, and
 * the request / apply / commit seam between the two editors and the SDL
 * backend.
 *
 * WHO OWNS WHAT.
 *   the store (sl_settings.h)   window_mode, window_width / _height (the
 *                               windowed client size), fullscreen_width /
 *                               _height (the exclusive mode), vsync - the
 *                               APPLIED truth, written by the backend only
 *                               after SDL confirmed it (sl_window_commit).
 *   this file                   host-clean arithmetic and state: the mode
 *                               names, the lists (deduped, ordered, bounded),
 *                               the fallbacks, the pending request and the
 *                               applied state the editors print. No SDL.
 *   the backend                 src/gfx/sl_gfx_sdl.c sdl_apply_display: the
 *   (src/gfx/sl_gfx_sdl.c)      one place SDL's window is asked to change,
 *                               once per frame at the frame reset, and the
 *                               one place its state is read back.
 *   the editors                 the front end's DISPLAY tab
 *                               (src/native/sl_front_options.c) and the
 *                               watch's SIGHTLINE -> DISPLAY child
 *                               (src/game/options.c) call sl_window_mode_step
 *                               / _size_step / _vsync_request and print
 *                               sl_window_state / _size_shown. No copies.
 *
 * THE TRANSACTION. An editor files a REQUEST (mode, size or vsync); the
 * backend takes it at its next frame reset, remembers the working state,
 * asks SDL, reads back what SDL actually did, and COMMITS to the store only
 * when the read-back matches - otherwise it restores the remembered state
 * and the store is untouched (a failed request simply does not take; the
 * editors keep printing the applied truth). The window's size is never
 * duplicated here: the renderer reads it from GL at every frame reset as it
 * did before #52 (sl_gfx_dl.c g_window_vp), the #45 fit derives the content
 * and safe rects from that one number, and the pointer layers read the safe
 * rect back - so a mode change reaches every consumer through the pipeline
 * that already existed, exactly once, on the next frame.
 *
 * THE MODES, under the #45 window contract (the aspect is the SHAPE at the
 * current height; the fit rule for a framebuffer that cannot be resized):
 *   WINDOWED    a windowed client; the height is the RESOLUTION row's (or
 *               the launcher's SL_WINDOW_SIZE when none was chosen - the
 *               accepted launch, bit for bit) and the width is height x the
 *               aspect. Not resizable by hand, as before.
 *   BORDERLESS  SDL's fullscreen-desktop: the window takes the desktop at
 *               the desktop's own mode, no mode switch, the game FITS (the
 *               #45 rule). The RESOLUTION row is informational - the desktop
 *               owns the size, nothing is scaled underneath.
 *   FULLSCREEN  exclusive fullscreen at a real display mode: the RESOLUTION
 *               row's pair (or the desktop's mode when none was chosen, or
 *               when the stored pair is not one the display offers) at the
 *               desktop's refresh rate. The game fits inside it.
 *
 * THE LISTS, derived from the display the window is on (never a hard-coded
 * 1920x1080), bounded, deduped, ascending:
 *   fullscreen  the display's modes deduped by width x height (the refresh
 *               rate is not a choice: the desktop's), 640x480 and up.
 *   windowed    the distinct HEIGHTS of those modes that fit the desktop -
 *               height <= the desktop's, and height x aspect <= the
 *               desktop's width - each shown as (height x aspect) x height,
 *               the window that would result. The list follows the aspect.
 *
 * HOST-CLEAN like sl_settings.h / sl_display.h: ints only, so src/platform,
 * src/gfx, src/native and the self-test (displaytest.ps1) all include it.
 */
#ifndef SL_WINDOW_H
#define SL_WINDOW_H

#include "sl_settings.h"      /* SL_WINDOW_* ids */

#define SL_WINDOW_LIST_MAX  32
#define SL_WINDOW_MIN_W     640
#define SL_WINDOW_MIN_H     480

struct sl_window_list {
    int n;
    int w[SL_WINDOW_LIST_MAX];
    int h[SL_WINDOW_LIST_MAX];
};

/* ---- the store, typed ---------------------------------------------------- */
int         sl_window_mode(void);                       /* clamped; WINDOWED when inactive / bad */
const char *sl_window_mode_name(int mode);              /* "WINDOWED" / "BORDERLESS" / "FULLSCREEN"; "?" out of range */
int         sl_window_stored_windowed(int *w, int *h);  /* 1 when a windowed pair is chosen (both > 0) */
int         sl_window_stored_fullscreen(int *w, int *h);/* 1 when an exclusive pair is chosen */
int         sl_vsync(void);                             /* 0 / 1 */

/* ---- the lists: pure ----------------------------------------------------- */
/* The display's modes (any order, duplicates across refresh rates) -> the
 * fullscreen list: deduped by w x h, at least SL_WINDOW_MIN_W x _H, ascending
 * by width then height, at most SL_WINDOW_LIST_MAX (the largest kept). */
void sl_window_list_modes(const int *mw, const int *mh, int n, struct sl_window_list *out);
/* The windowed list from the fullscreen list: distinct heights that fit the
 * desktop at `aspect`, each as (sl_display_window_width(h, aspect), h). */
void sl_window_list_windowed(const struct sl_window_list *modes, int desk_w, int desk_h, int aspect,
                             struct sl_window_list *out);
/* The index of an exact w x h in a list, -1 if absent. */
int  sl_window_list_find(const struct sl_window_list *l, int w, int h);
/* The next entry from a size that may be off the list: dir > 0 the first
 * entry with a greater height (or width at the same height), dir < 0 the
 * last with a smaller one; the end entry when there is none further.
 * Returns -1 on an empty list. */
int  sl_window_list_step(const struct sl_window_list *l, int w, int h, int dir);

/* ---- the fallbacks: pure ------------------------------------------------- */
/* FULLSCREEN: the stored pair when the display offers it, else the desktop
 * mode. Returns 1 when the stored pair was used, 0 on the fallback (also
 * when nothing was stored - not a fault, the default). */
int  sl_window_pick_fullscreen(const struct sl_window_list *modes, int want_w, int want_h,
                               int desk_w, int desk_h, int *w, int *h);
/* WINDOWED: the client height - the stored height when it is one the
 * windowed list offers, else the launcher's; the width is the aspect's
 * (sl_display_window_width). Returns 1 when the stored height was used. */
int  sl_window_pick_windowed(const struct sl_window_list *wlist, int want_h, int launch_h, int aspect,
                             int *w, int *h);

/* ---- what the backend publishes ----------------------------------------- */
/* The lists of the display the window is on and that display's desktop
 * size, published at init and whenever the window's display changes. */
void sl_window_publish_lists(const struct sl_window_list *modes, int desk_w, int desk_h);
/* The applied state after every apply: the mode SDL reports, the client /
 * drawable size, the swap interval read back. */
void sl_window_publish_state(int mode, int w, int h, int vsync);
/* Read them back (the editors). state returns 0 until the backend has
 * published once (no window: headless, the null backend). */
int  sl_window_state(int *mode, int *w, int *h, int *vsync);
int  sl_window_desktop(int *w, int *h);
const struct sl_window_list *sl_window_list_fullscreen(void);
const struct sl_window_list *sl_window_list_windowed_now(void);

/* ---- requests (the editors) and the take / commit (the backend) --------- */
#define SL_WINDOW_REQ_MODE   1
#define SL_WINDOW_REQ_SIZE   2
#define SL_WINDOW_REQ_VSYNC  4
void sl_window_request_mode(int mode);          /* refused out of range */
void sl_window_request_size(int w, int h);      /* for the CURRENT mode's row; ignored in BORDERLESS */
void sl_window_request_vsync(int on);
/* The pending request's fields (a mask of SL_WINDOW_REQ_*), cleared. */
int  sl_window_request_take(int *mode, int *w, int *h, int *vsync);
int  sl_window_request_pending(void);
/* The backend persists what SDL confirmed: the mode; when a size was
 * REQUESTED (size_chosen), the pair SDL gave - in WINDOWED the client pair as
 * window_*, in FULLSCREEN the mode pair as fullscreen_*, in BORDERLESS
 * nothing (the desktop owns it); when vsync was REQUESTED (vsync_chosen),
 * the interval read back. A fallback the backend took on its own is never
 * written back, so the file keeps saying what the player chose.
 * Write-on-change through the store, one write for the batch. */
void sl_window_commit(int mode, int size_chosen, int w, int h, int vsync_chosen, int vsync);

/* ---- the editors' steps ------------------------------------------------- */
void sl_window_mode_step(int dir);              /* the next mode, wrapping; files a request */
void sl_window_size_step(int dir);              /* the next size of the current mode's list; nothing in BORDERLESS */
void sl_window_vsync_toggle(void);
/* What the RESOLUTION row prints: the applied size (WINDOWED the client,
 * BORDERLESS the desktop, FULLSCREEN the mode). 0 until published. */
int  sl_window_size_shown(int *w, int *h);
/* Is the RESOLUTION row a control (1) or informational (0: BORDERLESS, or
 * no list yet)? */
int  sl_window_size_editable(void);
/* The mode the editors show: the applied one when published, else the store's. */
int  sl_window_mode_shown(void);
/* "1280x720" into buf (at least 12 bytes), for the editors. */
void sl_window_size_text(int w, int h, char *buf, int n);

/* ---- the quit request --------------------------------------------------
 * The front end's QUIT GAME row (front.c interface_menu06_modesel, native
 * arm) files it; the frame pump (sl_ultra_shim.c) honours it at the next
 * frame boundary, taking the path closing the window takes. Latching. */
void sl_quit_request(void);
int  sl_quit_requested(void);

#ifdef SL_DISPLAY_SELFTEST
void sl_window_selftest(int *checks, int *fails);
/* The self-test's reset, so the quit check leaves nothing set behind it. */
void sl_quit_request_clear_for_test(void);
#endif

#endif /* SL_WINDOW_H */
