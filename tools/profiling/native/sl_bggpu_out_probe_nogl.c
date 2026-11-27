/* THE NO-GL BUILD'S GL SIDE FOR THE VERTEX-OUTPUT PROBE: there isn't one.
 *
 * bggpuoutprobe.exe links this instead of sl_bggpu_depth_probe_gl.c, so the
 * default build carries NO GL entry point, NO window code and NO opengl32
 * import - it cannot reach a driver even if its --gl argument were passed, and
 * the harness proves that from the binary's import table rather than asserting
 * it. The two builds share one main because the GL side is reached only through
 * these two functions.
 *
 * It also supplies the DEPTH probe's two GL hooks, because the vertex-output
 * binary links sl_bggpu_depth_probe.c for its case table and reference and that
 * file's header declares them. Neither is called from the output probe's main. */
#include "sl_bggpu_out_probe.h"

#include <stdio.h>

int sl_bgop_gl_built(void) { return 0; }

int sl_bgop_gl_run(const struct sl_bgdp_case *cases, const struct sl_bgop_attr *attrs,
                   int n, float *out, unsigned *counts, char *why, int whycap)
{
    (void) cases; (void) attrs; (void) n; (void) out;
    if (counts != NULL) counts[0] = counts[1] = counts[2] = 0u;
    if (why != NULL && whycap > 0)
        snprintf(why, (size_t) whycap,
                 "this build links sl_bggpu_out_probe_nogl.c and has no GL side");
    return 1;
}

int sl_bgdp_gl_built(void) { return 0; }

int sl_bgdp_gl_run(const struct sl_bgdp_case *cases, int n, float *out_z,
                   char *why, int whycap)
{
    (void) cases; (void) n; (void) out_z;
    if (why != NULL && whycap > 0)
        snprintf(why, (size_t) whycap,
                 "this build links sl_bggpu_out_probe_nogl.c and has no GL side");
    return 1;
}
