/* THE NO-GL BUILD'S GL SIDE: there isn't one.
 *
 * bggpudepthprobe.exe links this instead of sl_bggpu_depth_probe_gl.c, so the
 * default build carries NO GL entry point, NO window code and NO opengl32
 * import - it cannot reach a driver even if its --gl argument were passed, and
 * the entrypoint proves that from the binary's import table rather than
 * asserting it. The two builds share one main because the GL side is reached
 * only through these two functions. */
#include "sl_bggpu_depth_probe.h"

#include <stdio.h>

int sl_bgdp_gl_built(void) { return 0; }

int sl_bgdp_gl_run(const struct sl_bgdp_case *cases, int n, float *out_z,
                   char *why, int whycap)
{
    (void) cases; (void) n; (void) out_z;
    if (why != NULL && whycap > 0)
        snprintf(why, (size_t) whycap,
                 "this build links sl_bggpu_depth_probe_nogl.c and has no GL side");
    return 1;
}
