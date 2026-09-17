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

/* The rectangle the game image is presented into, in WINDOW pixels, top-left
 * origin. Defined in sl_gfx_dl.c beside the viewport it is derived from; see
 * the comment there. Returns 0 when it is not established yet. */
int  sl_gfx_present_rect(int win_h, int *x, int *y, int *w, int *h);

#endif /* SL_GFX_H */
