/* High-refresh presentation: the camera of a presentation-only frame.
 *
 * WHAT A PRESENTATION FRAME IS. The simulation keeps its own cadence - one
 * game frame per paced VI, its step the VI count the frame spanned
 * (frametiming.c waitForNextFrame -> speedgraphframes -> g_ClockTimer) - and
 * builds one display list per game frame exactly as before. While the frame
 * pump would otherwise sleep until the next VI, the platform layer
 * (src/platform/sl_ultra_shim.c, sl_pres_between) may present EXTRA frames by
 * re-interpreting that same, already-built display list (sl_gfx_dl.c,
 * sl_gfx_dl_rewalk). The game thread is blocked inside the pump for the whole
 * of that time, so every buffer the list references - the list itself, the
 * dynamic matrices and vertices, the texture pool, the room data - is exactly
 * as it was when the list was first walked. Nothing of the game runs and
 * nothing of the game is written.
 *
 * WHAT CHANGES IN ONE. Only the view. Two projections in the list carry the
 * player's camera, and the walker already tells them apart by the address
 * the game named (sl_gfx_note_world_projection / _projection2):
 *
 *   which 1  fr.c's plain perspective P. Props, characters and glass load
 *            modelview matrices that already contain the absolute view V
 *            (camGetWorldToScreenMtxf, bondview2.c field_64), so the vertex
 *            reaching the projection is in EYE space: clip = x V P (with x, y
 *            and z of that space times the stage's visibility scale - see
 *            sl_pres_prepare).
 *   which 2  bondview2.c's field_10E0 = V_rel P, the view built about the
 *            frame's render origin (current_model_pos, times D_800364CC); the
 *            room geometry and the explosion / glass passes are submitted in
 *            that origin-relative space: clip = x_rel V_rel P.
 *
 * For an interpolated eye/look/up the views become V_I and V_rel_I, and
 *
 *   which 1  clip = x V_I P   = (x V) (V^-1 V_I) P       -> P'  = C1 P,  C1 = V^-1 V_I
 *   which 2  clip = x V_rel_I P = x (V_rel_I V_rel^-1) V_rel P
 *                                                       -> PV' = D PV,   D = V_rel_I V_rel^-1
 *
 * so each is ONE left-multiply of the matrix the list loaded, with no need to
 * know P itself. Row-vector order throughout, the walker's convention
 * (v' = v * M, mtx_mul(a, b) = a * b). The first-person weapon is drawn under
 * its own projection (bondview2.c, its guPerspective) and inside the walker's
 * view-model bracket, and the HUD, the watch and the front end load
 * projections the game never named, so none of them is touched: they stay
 * locked to the screen, as they are.
 *
 * THE SNAPSHOTS. bondviewUpdateCameraMatrices hands over the camera its
 * matrices were built from (sl_gfx_note_camera, a read-only copy), and the
 * walker latches the pending one at the top of every ORIGINAL walk
 * (sl_pres_latch). A pair is usable only when both frames saw exactly one
 * camera, they are consecutive walks, the camera mode and render scale agree,
 * and the camera did not jump: a teleport, a cut, a respawn, a mode change, a
 * level load or a split screen therefore present nothing extra - the frame is
 * shown at the simulation rate exactly as without this file.
 *
 * NOTHING HERE FEEDS THE SIMULATION. The interpolated camera exists only in
 * the projection the walker hands GL for a presentation frame; collision, aim,
 * the AI's visibility tests and every game matrix read the game's own values,
 * which this file never writes.
 */
#ifndef __sgi
#include <math.h>
#include <string.h>

struct sl_pres_cam {
    double eye[3], look[3], up[3], origin[3];
    double scale;
    int    mode;
    int    n;            /* camera notes in the frame: exactly 1 is usable */
    unsigned walk;       /* the original walk that latched it */
};

static struct sl_pres_cam s_pend, s_prev, s_cur;
static unsigned s_walks;              /* original walks latched */

/* The correction in force for the presentation walk being made. */
static int    s_corr_on;
static double s_c1[16], s_d[16];

/* Jump limits for a usable pair. A frame's camera moves a few world units
 * (running) and a few degrees (mouse look) per VI; anything past these is a
 * cut, a teleport or a respawn, where blending two views would draw a place
 * neither frame showed. */
#define SL_PRES_MAX_MOVE   300.0           /* world units between two frames */
#define SL_PRES_MIN_COS    0.5             /* 60 degrees of look or up */

void sl_gfx_note_camera(const float *eye, const float *look, const float *up,
                        const float *origin, float scale, int mode);
void sl_gfx_note_camera(const float *eye, const float *look, const float *up,
                        const float *origin, float scale, int mode)
{
    int k;
    for (k = 0; k < 3; k++) {
        s_pend.eye[k] = eye[k];
        s_pend.look[k] = look[k];
        s_pend.up[k] = up[k];
        s_pend.origin[k] = origin[k];
    }
    s_pend.scale = scale;
    s_pend.mode = mode;
    s_pend.n++;
}

/* Top of every ORIGINAL walk (never a re-walk): the camera noted while this
 * frame's list was built becomes current, the previous one becomes the pair's
 * start. */
void sl_pres_latch(void);
void sl_pres_latch(void)
{
    s_walks++;
    s_prev = s_cur;
    s_cur = s_pend;
    s_cur.walk = s_walks;
    memset(&s_pend, 0, sizeof s_pend);
    s_corr_on = 0;
}

static double dot3(const double *a, const double *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static double len3(const double *a)
{
    return sqrt(dot3(a, a));
}

/* Is there a usable (previous, current) pair for the frame just walked? */
int sl_pres_pair_valid(void);
int sl_pres_pair_valid(void)
{
    double d[3], lp, lc, up, uc;
    int k;
    if (s_cur.n != 1 || s_prev.n != 1) return 0;
    if (s_cur.walk != s_prev.walk + 1u) return 0;
    if (s_cur.mode != s_prev.mode) return 0;
    if (s_cur.scale != s_prev.scale || s_cur.scale <= 0.0) return 0;
    for (k = 0; k < 3; k++) d[k] = s_cur.eye[k] - s_prev.eye[k];
    if (len3(d) > SL_PRES_MAX_MOVE) return 0;
    lp = len3(s_prev.look); lc = len3(s_cur.look);
    up = len3(s_prev.up);   uc = len3(s_cur.up);
    if (lp < 1e-9 || lc < 1e-9 || up < 1e-9 || uc < 1e-9) return 0;
    if (dot3(s_prev.look, s_cur.look) / (lp * lc) < SL_PRES_MIN_COS) return 0;
    if (dot3(s_prev.up, s_cur.up) / (up * uc) < SL_PRES_MIN_COS) return 0;
    return 1;
}

/* matrix_4x4_set_lookat (matrixmath.c), in double: the same normalisation
 * order, the same row-vector layout. */
static void lookat_row(const double *eye, const double *look, const double *upv, double m[16])
{
    double f[3], r[3], u[3], n;
    n = -1.0 / len3(look);
    f[0] = look[0] * n; f[1] = look[1] * n; f[2] = look[2] * n;
    r[0] = upv[1] * f[2] - upv[2] * f[1];
    r[1] = upv[2] * f[0] - upv[0] * f[2];
    r[2] = upv[0] * f[1] - upv[1] * f[0];
    n = 1.0 / len3(r);
    r[0] *= n; r[1] *= n; r[2] *= n;
    u[0] = f[1] * r[2] - f[2] * r[1];
    u[1] = f[2] * r[0] - f[0] * r[2];
    u[2] = f[0] * r[1] - f[1] * r[0];
    n = 1.0 / len3(u);
    u[0] *= n; u[1] *= n; u[2] *= n;
    m[0] = r[0]; m[4] = r[1]; m[8]  = r[2]; m[12] = -dot3(eye, r);
    m[1] = u[0]; m[5] = u[1]; m[9]  = u[2]; m[13] = -dot3(eye, u);
    m[2] = f[0]; m[6] = f[1]; m[10] = f[2]; m[14] = -dot3(eye, f);
    m[3] = 0.0;  m[7] = 0.0;  m[11] = 0.0;  m[15] = 1.0;
}

/* Inverse of a lookat: the 3x3 is orthonormal, so R^T, and the translation
 * row is the eye. Computed from the matrix rather than the eye so the
 * product with the matrix is the identity to rounding. */
static void rigid_inverse(const double m[16], double out[16])
{
    int r, c;
    for (r = 0; r < 3; r++)
        for (c = 0; c < 3; c++)
            out[r * 4 + c] = m[c * 4 + r];
    for (c = 0; c < 3; c++)
        out[12 + c] = -(m[12] * out[0 * 4 + c] + m[13] * out[1 * 4 + c] + m[14] * out[2 * 4 + c]);
    out[3] = out[7] = out[11] = 0.0;
    out[15] = 1.0;
}

static void mul_d(const double a[16], const double b[16], double out[16])
{
    double t[16];
    int r, c, k;
    for (r = 0; r < 4; r++)
        for (c = 0; c < 4; c++) {
            double s = 0.0;
            for (k = 0; k < 4; k++) s += a[r * 4 + k] * b[k * 4 + c];
            t[r * 4 + c] = s;
        }
    memcpy(out, t, sizeof t);
}

/* Prepare the correction for one presentation walk at blend alpha (0 = the
 * previous frame's camera, 1 = this frame's). alpha >= 1, or no usable pair,
 * leaves the list's own matrices in force bit for bit. Returns 1 when a
 * correction is in force. */
int sl_pres_prepare(double alpha);
int sl_pres_prepare(double alpha)
{
    double e[3], l[3], u[3], v_n[16], v_i[16], inv[16];
    double er_n[3], er_i[3];
    int k;

    s_corr_on = 0;
    if (alpha >= 1.0 || !sl_pres_pair_valid()) return 0;
    if (alpha < 0.0) alpha = 0.0;
    for (k = 0; k < 3; k++) {
        e[k] = s_prev.eye[k]  + (s_cur.eye[k]  - s_prev.eye[k])  * alpha;
        l[k] = s_prev.look[k] / len3(s_prev.look)
             + (s_cur.look[k] / len3(s_cur.look) - s_prev.look[k] / len3(s_prev.look)) * alpha;
        u[k] = s_prev.up[k] / len3(s_prev.up)
             + (s_cur.up[k] / len3(s_cur.up) - s_prev.up[k] / len3(s_prev.up)) * alpha;
    }
    /* which 1: absolute eye space. */
    lookat_row(s_cur.eye, s_cur.look, s_cur.up, v_n);
    lookat_row(e, l, u, v_i);
    rigid_inverse(v_n, inv);
    mul_d(inv, v_i, s_c1);
    /* The game scales x, y and z of every matrix it hands the RSP by the
     * stage's visibility scale (matrix_4x4_f32_to_s32; D_800364CC is set to
     * the same number, bg.c:1090), leaving w alone - so the eye space props
     * arrive in is the true one times that scale, and a move of the eye is
     * that much smaller in it. 1.0 on every stage but the Dam and Surface. */
    for (k = 0; k < 3; k++) s_c1[12 + k] *= s_cur.scale;
    /* which 2: about the frame's render origin, at its scale - the space
     * bondviewUpdateCameraMatrices built field_10E0 in. */
    for (k = 0; k < 3; k++) {
        er_n[k] = (s_cur.eye[k] - s_cur.origin[k]) * s_cur.scale;
        er_i[k] = (e[k]         - s_cur.origin[k]) * s_cur.scale;
    }
    lookat_row(er_n, s_cur.look, s_cur.up, v_n);
    lookat_row(er_i, l, u, v_i);
    rigid_inverse(v_n, inv);
    mul_d(v_i, inv, s_d);
    s_corr_on = 1;
    return 1;
}

void sl_pres_clear(void);
void sl_pres_clear(void)
{
    s_corr_on = 0;
}

/* The walker's hook (proj_apply_aspect): out = correction * in for a world
 * projection while a correction is in force. Returns 0, leaving out
 * untouched, otherwise. */
int sl_pres_correct(int which, const float in[16], float out[16]);
int sl_pres_correct(int which, const float in[16], float out[16])
{
    const double *c;
    int r, col, k;
    if (!s_corr_on || (which != 1 && which != 2)) return 0;
    c = (which == 1) ? s_c1 : s_d;
    for (r = 0; r < 4; r++)
        for (col = 0; col < 4; col++) {
            double s = 0.0;
            for (k = 0; k < 4; k++) s += c[r * 4 + k] * (double) in[k * 4 + col];
            out[r * 4 + col] = (float) s;
        }
    return 1;
}
#endif
