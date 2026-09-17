/* Backend selection and the wrappers the platform layer calls.
 *
 * Runtime selection, not compile-time: one binary must both replay traces
 * headlessly and open a window, because trace-verify and the demo are the same
 * build. SL_WINDOW=1 asks for a window; anything that fails falls back to null
 * so a headless box never loses the simulation.
 */
#ifndef __sgi
#include "sl_gfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const struct sl_gfx_backend sl_gfx_null;
extern const struct sl_gfx_backend sl_gfx_sdl;

static const struct sl_gfx_backend *g_be;
static int g_active;

const struct sl_gfx_backend *sl_gfx_select(void)
{
    const char *want = getenv("SL_WINDOW");
    if (want != NULL && strcmp(want, "0") != 0)
        return &sl_gfx_sdl;
    return &sl_gfx_null;
}

int sl_gfx_init(void)
{
    const char *ws;
    int w = 640, h = 480;

    if (g_be != NULL)
        return g_active;
    g_be = sl_gfx_select();

    ws = getenv("SL_WINDOW_SIZE");           /* e.g. 1280x960 */
    if (ws != NULL) {
        int pw, ph;
        if (sscanf(ws, "%dx%d", &pw, &ph) == 2 && pw > 0 && ph > 0) {
            w = pw; h = ph;
        }
    }

    /* "active" means a REAL window is up. null_init succeeds by design, so
     * testing its return value alone marked headless runs as active and let
     * rendering work run during trace replay. */
    g_active = g_be->init(w, h, "Sightline") && (g_be != &sl_gfx_null);
    if (!g_active && g_be != &sl_gfx_null) {
        /* a window was asked for and could not be had - keep simulating */
        g_be = &sl_gfx_null;
        g_be->init(w, h, "Sightline");
    }
    return g_active;
}

void sl_gfx_begin_frame(void) { if (g_be) g_be->begin_frame(); }
/* The backend presents, and then the bug mark is flushed AGAIN.
 *
 * Not redundant. The SDL backend writes the mark itself, before the swap,
 * because it is the only place the marked frame's back buffer still exists.
 * The null backend has no such place - and a headless run can still be F8-ed
 * through the same code path once a window is absent for any reason. The
 * second call is a no-op when the first one already consumed the capture, and
 * writes a mark with no screenshot when it did not, which is the honest
 * outcome rather than silence. */
void sl_gfx_end_frame(void)
{
    extern void sl_run_mark_full(int (*shoot)(const char *path),
                                 int (*probe)(char *out, int n));
    if (g_be) g_be->end_frame();
    /* No window backend means no framebuffer to photograph AND no centre pixel
     * to read. Both are passed as absent rather than faked; the record says
     * "no backend" in both sections. */
    sl_run_mark_full(NULL, NULL);
}
int  sl_gfx_poll(void)        { return g_be ? g_be->poll() : 1; }
int  sl_gfx_active(void)      { return g_active; }

void sl_gfx_shutdown(void)
{
    if (g_be) g_be->shutdown();
    g_be = NULL;
    g_active = 0;
}
#endif
