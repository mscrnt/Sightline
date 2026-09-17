/* Headless backend. The DEFAULT, because trace replay, trace-verify and CI all
 * run without a display and must never gain a window as a side effect. */
#ifndef __sgi
#include "sl_gfx.h"

static int  null_init(int w, int h, const char *t) { (void) w; (void) h; (void) t; return 1; }
static void null_begin(void) {}
static void null_end(void) {}
static int  null_poll(void) { return 1; }
static void null_shutdown(void) {}

const struct sl_gfx_backend sl_gfx_null = {
    "null", null_init, null_begin, null_end, null_poll, null_shutdown
};
#endif
