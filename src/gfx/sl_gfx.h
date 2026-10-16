/**
 * Backend-agnostic display interface (Phase 2 groundwork).
 *
 * src/game/ must never include this - the whole point of the layer is that the
 * simulation builds headless for the server and the AI track. Only
 * src/platform/ calls in here.
 *
 * Two backends:
 *   null  - no window, no GL. The default, because trace replay and
 *           trace-verify must keep running headless in CI and under xvfb.
 *   sdl   - SDL2 window + OpenGL context.
 *
 * Selected at RUNTIME by SL_WINDOW=1, not at compile time, so one binary both
 * replays traces and opens a window.
 */
#ifndef SL_GFX_H
#define SL_GFX_H

#ifdef __sgi
#error "sl_gfx.h is native-only"
#endif

struct sl_gfx_backend {
    const char *name;
    int  (*init)(int width, int height, const char *title);
    void (*begin_frame)(void);
    void (*end_frame)(void);          /* present */
    int  (*poll)(void);               /* 0 = quit requested */
    void (*shutdown)(void);
};

/* Chooses sdl when SL_WINDOW=1 and it initialises, else null. Never fails:
 * a headless box must still run the sim. */
const struct sl_gfx_backend *sl_gfx_select(void);

/* Convenience wrappers used by the platform layer. */
int  sl_gfx_init(void);
void sl_gfx_begin_frame(void);
void sl_gfx_end_frame(void);
int  sl_gfx_poll(void);
void sl_gfx_shutdown(void);
int  sl_gfx_active(void);             /* 1 when a real window is up */

/* High-refresh presentation (sl_gfx_present.c). Called by the frame pump
 * only while the game thread is blocked in it, between a frame's original
 * walk and its regular present.
 *   pres_ready       a window that can show it, exactly one list this frame,
 *                    and a usable camera pair (no cut, teleport or mode change)
 *   present_extra    re-walk the frame's list with the camera blended alpha of
 *                    the way from the previous frame's to this frame's (interp
 *                    0 = the list's own camera) and present it; hash, when
 *                    not NULL, receives backbuffer_hash of the image
 *   restore_regular  put the frame at its own camera back in the back buffer
 *                    for the regular present that follows: the copy kept
 *                    before the first extra frame, else a re-walk
 *   backbuffer_hash  diagnostic: FNV-1a of the back buffer (a full read-back) */
int  sl_gfx_pres_ready(void);
void sl_gfx_present_extra(double alpha, int interp, unsigned *hash);
void sl_gfx_restore_regular(void);
unsigned sl_gfx_backbuffer_hash(void);
/* Seconds the frame's original walk took, and the last re-walk: what the
 * pump budgets an extra frame and the restore on. */
double sl_gfx_walk_cost(void);
double sl_gfx_rewalk_cost(void);
/* 1 when restore_regular is expected to be a GPU copy, not a re-walk, and is
 * budgeted as such: the kept-frame copy has worked, or it has not been tried
 * yet where it can run. Budgeting an untried copy as a re-walk kept a view
 * whose walk leaves room for one extra frame but not for two walks from ever
 * trying it (Frigate's deck: no extra frame in a whole run). A copy that
 * fails on its first try turns itself off for the run, that one frame's
 * restore is a re-walk, and every later one is budgeted as a re-walk. */
int  sl_gfx_restore_is_copy(void);
/* The next sl_gfx_end_frame does everything but show the frame: the mark and
 * the shot still read its back buffer, the swap is skipped and the screen
 * keeps the last frame shown. One frame only. The pump calls it for a cap
 * below the VI rate (sl_ultra_shim.c, sl_pres_hold). */
void sl_gfx_hold_present(void);

/* The rectangle the game image is presented into, in WINDOW pixels, top-left
 * origin. Defined in sl_gfx_dl.c beside the viewport it is derived from; see
 * the comment there. Returns 0 when it is not established yet. */
int  sl_gfx_present_rect(int win_h, int *x, int *y, int *w, int *h);
/* The content rect (#45) the same way: the selected aspect fitted in the
 * window, which the presented (safe) rect sits centred inside. The drawn
 * pointer cursor may cross all of it; the hit tests keep the safe rect. */
int  sl_gfx_content_rect(int win_h, int *x, int *y, int *w, int *h);

#endif /* SL_GFX_H */
