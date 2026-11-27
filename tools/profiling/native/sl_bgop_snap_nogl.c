/* THE NO-GL BUILD'S SNAPSHOT DRIVER SIDE: there isn't one.
 *
 * bggpuoutprobe.exe links this; bggpuoutprobe-gl.exe gets the real
 * sl_bgop_gl_run_snapshot out of the output-guarded section of
 * sl_bggpu_depth_probe_gl.c. The split is the same one sl_bggpu_out_probe_nogl.c
 * already makes for the synthetic run, and for the same reason: the default
 * binary carries no GL entry point and no opengl32 import, so it cannot reach a
 * driver even if asked, and the harness proves that from the import table
 * rather than from this comment.
 *
 * It lives in its own translation unit rather than beside the other stub so
 * that the stub set and the GL set stay the only difference between the two
 * links. Nothing here allocates, opens or measures anything.
 */
#include "sl_bggpu_out_probe.h"

#include <stdio.h>
#include <string.h>

int sl_bgop_gl_run_snapshot(const struct sl_bgsnap *s,
                            struct sl_bgop_snap_tally *t, int report,
                            char *why, int whycap)
{
    (void) s; (void) report;
    if (t != NULL) memset(t, 0, sizeof *t);
    if (why != NULL && whycap > 0)
        snprintf(why, (size_t) whycap,
                 "this build links sl_bgop_snap_nogl.c and has no GL side, so "
                 "no draw was captured and nothing was compared");
    return 1;
}
