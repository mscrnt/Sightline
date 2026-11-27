/* THE BACKGROUND PASS'S GPU DEPTH, ON A DRIVER - the probe's contract.
 *
 * WHAT GAP THIS CLOSES. src/gfx/sl_gfx_bggpu.c computes the background pass's
 * clip position AND its depth inside a GLSL vertex program (BGG_VS). That
 * program has three depth cases - SL_BGGPU_DEPTH_SINGLE (the one-range frame's
 * far clamp), _FAR0 (the first range, z = k1*w - k2, with the mesh's own
 * opaque-or-blend coefficient pair) and _FAR1 (the game's range, unclamped).
 * tools/profiling/native/test_sl_gfx_bggpu.c records only that the program TEXT
 * reached glShaderSource, and the one arithmetic comparison it makes is between
 * a HAND TRANSCRIPTION of those lines and the CPU reference, which is a check
 * on two texts agreeing. No driver has evaluated the expressions. This probe is
 * the setup that lets a driver evaluate them, and compare the result with the
 * CPU path bit for bit.
 *
 * NO ROM DATA AND NO ASSET. Every input here is SYNTHETIC: constructed
 * matrices, model positions chosen at the branch boundaries, and coefficients
 * derived by calling sl_wa_far_plane / sl_wa_far_range. Synthetic probe data is
 * not ROM data; nothing in this probe opens a ROM, a background bundle or any
 * installed file.
 *
 * THE SHADER TEXT IS THE PRODUCTION TEXT, NOT A COPY. BGG_VS is file-static
 * with no external linkage and src/ is not edited to expose it, so
 * sl_bggpu_depth_probe_pin.py reads the literal out of src/gfx/sl_gfx_bggpu.c
 * at build time, hashes it against sl_bggpu_depth_probe.pin, and emits it as a
 * generated header. A shader-text or uniform-list change REFUSES the build. The
 * probe additionally proves at run time, with no GL, that the extracted bytes
 * are byte-for-byte the bytes the module itself hands to glShaderSource: the
 * module is linked in against the synthetic GL of sl_gfx_bggpu_testgl.c and its
 * recorded source pointer is compared with the extraction.
 *
 * TWO BUILDS, AND ONLY ONE OF THEM CAN REACH A GPU. They differ in exactly one
 * object file, which is what implements the two functions at the end of this
 * header.
 *   bggpudepthprobe.exe     links sl_bggpu_depth_probe_nogl.c. The case table,
 *                           the CPU reference and the probe's own unit checks.
 *                           No GL entry point is compiled in and opengl32 is
 *                           not linked, so this build cannot create a context
 *                           even by accident - the entrypoint proves that from
 *                           the binary's import table rather than asserting it.
 *   bggpudepthprobe-gl.exe  links sl_bggpu_depth_probe_gl.c (compiled with
 *                           SL_BGGPU_PROBE_GL). The same table and reference
 *                           plus a hidden-window GL context, the pinned text
 *                           compiled on the driver, gl_Position captured
 *                           through transform feedback, and the comparison. It
 *                           ALSO requires the runtime argument --gl before it
 *                           touches a driver, so the binary existing is not the
 *                           same as a run having happened.
 *
 * EXACT COMPARISON, NO TOLERANCE. Both sides compute float32 in the same
 * association: the CPU reference mirrors sl_gfx_dl.c's own expressions in their
 * own order, compiled -msse2 -mfpmath=sse (build.ps1's flags for src/gfx, so no
 * x87 excess precision) with -ffp-contract=off; the GLSL marks x, y, z and c
 * `precise`, which forbids the driver from reassociating or fusing the same
 * expressions. The comparison is therefore on the 32-bit pattern, through
 * memcmp, so a NaN on either side is compared as bits and never compares equal
 * by accident.
 */
#ifndef SL_BGGPU_DEPTH_PROBE_H
#define SL_BGGPU_DEPTH_PROBE_H

/* Which GLSL branch a case drives. The values are SL_BGGPU_DEPTH_* from
 * src/gfx/sl_gfx_bggpu.h and are checked against it at compile time. */
#define SL_BGDP_MAX_CASES 64

/* One synthetic probe input. `C`, `sc`, `post` and `org` are the walk uniforms
 * sl_bggpu_use_walk uploads; `dmode`, `fk1` and `fk2` are what
 * sl_bggpu_use_depth uploads for the draw; `px/py/pz` is the model position
 * that arrives as gl_Vertex. */
struct sl_bgdp_case {
    const char  *name;        /* "single/clamp-at"                            */
    const char  *probes;      /* the boundary this input is here for          */
    const char  *matrix;      /* which of the probe's matrices, for the report*/
    const float *C;           /* 16 floats, column order as the module's      */
    float        org[3];
    float        sc, post;
    float        px, py, pz;
    int          dmode;
    int          fogless;
    float        fk1, fk2;
    /* An ADVISORY case is measured and reported but does not decide the
     * verdict, because what it probes is not specified for a driver. The only
     * one today is a denormal w: GLSL's default precision does not require
     * denormal support, so a driver that flushes to zero takes the other side
     * of the `c.w > 0.0` test. That is a fact worth measuring and not a defect
     * in either side, so it is kept out of the pass/fail count. */
    int          advisory;
};

/* What the CPU path leaves in the depth slot for one case, and how it got
 * there. Mirrors src/gfx/sl_gfx_dl.c; see sl_bggpu_depth_probe.c for the line
 * numbers each expression is taken from. */
struct sl_bgdp_ref {
    float c[4];      /* the clip position, the pass's own product            */
    float zraw;      /* the unclamped z the pass keeps (sl_gfx_dl.c:17847)   */
    float z;         /* the depth this case's mode leaves behind            */
    int   clamped;   /* the SINGLE clamp fired for this input               */
};

/* The table, built from the real sl_wa_far_plane / sl_wa_far_range answers at
 * the real SL_WA_FAR_OVERLAP / SL_WA_FAR_OVERLAP_BLEND. Returns the count. */
int sl_bgdp_build_cases(void);
const struct sl_bgdp_case *sl_bgdp_cases(void);

/* The CPU reference for one case. No tolerance anywhere. */
void sl_bgdp_reference(const struct sl_bgdp_case *k, struct sl_bgdp_ref *r);

/* The case-table builder's own counters. A probe that reuses this table
 * without compiling this file's main (SL_BGDP_NO_MAIN) still has to be able to
 * see that the builder itself reported a failure - the table overflowing is
 * one - rather than reading a short table as a complete one. */
void sl_bgdp_builder_ledger(int *out_checks, int *out_fails);

/* 0 when the measured depth is the reference's depth BIT FOR BIT. */
int sl_bgdp_equal(float measured, float expected);

/* The pinned production shader text and uniform names (see the header note). */
const char        *sl_bgdp_glsl(void);
unsigned           sl_bgdp_glsl_bytes(void);
const char        *sl_bgdp_glsl_sha256(void);
const char        *sl_bgdp_uniforms_sha256(void);
const char        *sl_bgdp_source_sha256(void);
int                sl_bgdp_uniform_count(void);
const char *const *sl_bgdp_uniform_names(void);

/* ---- the GL side, which only bggpudepthprobe-gl.exe has ------------------ */

/* 1 in the GL build, 0 otherwise. The no-GL build's stub answers 0 and its
 * sl_bgdp_gl_run refuses, so the two builds share one main. */
int sl_bgdp_gl_built(void);

/* ONE DRIVER RUN. Creates its own hidden-window context, compiles the pinned
 * text, links it with gl_Position captured through transform feedback, uploads
 * each case's uniforms, draws one point per case with the rasterizer discarded,
 * reads the captured clip position back and writes the depth component into
 * out_z[i]. Returns 0 on success, non-zero on any GL failure, with the reason
 * in `why`. Writes nothing outside out_z and why, and leaves no file behind. */
int sl_bgdp_gl_run(const struct sl_bgdp_case *cases, int n, float *out_z,
                   char *why, int whycap);

#endif /* SL_BGGPU_DEPTH_PROBE_H */
