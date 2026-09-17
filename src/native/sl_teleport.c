/**
 * Developer-only viewpoint teleport, from an F8 mark, native side.
 *
 * WHAT THIS IS FOR. An F8 mark records a location and a facing (see
 * src/native/sl_game_query.c and tools/windows/play.ps1 -TeleportMark). To
 * work on a bug the owner marked - Dam water, #11 - the agent must be able to
 * DIRECT-BOOT the level and stand where the mark stands, WITHOUT replaying the
 * owner's input stream. The replay drifts from the live session (measured
 * divergence by frame ~12495), so it is not a navigation tool; the mark is.
 *
 * WHY NATIVE, NOT src/game. src/platform compiles against host headers and
 * cannot see a game struct; src/game must stay byte-identical to the matching
 * build. src/native is compiled with the game include path but is not game
 * logic and is included by nothing in src/game, so `make check-layering` is
 * untouched. This file therefore reads and WRITES game state through the
 * game's OWN spawn/registration functions - the same calls mp_respawn_handler
 * (bondview2.c) and the solo spawn (bondview_r.c) use - rather than editing
 * matrices behind the game's back. The next NORMAL camera build then produces
 * the marked view on its own; nothing here forces viewtoworldmtxf.
 *
 * AUTHORITY, established in the decomp (D:\Projects\007 read-only):
 *   - In FP (player cameramode 0) bondviewUpdateCameraMatrices takes cam_pos
 *     straight from field_488.pos and matrix_4x4_set_basis_and_position writes
 *     it into m[3] of viewtoworldmtxf (bondview2.c:8567,8448). So the mark's
 *     eye-world-pos IS field_488.pos - not an arbitrary player field.
 *   - field_488.pos is refreshed every frame from collision_position, whose Y
 *     is field_70 (floor) + eyeheight (bondview2.c:4584-4600). A teleport that
 *     wrote pos.y directly would be overwritten next frame; setting field_70
 *     to the stan floor and letting the game recompute is what STAYS put.
 *   - applied_view / applied_view2 (cam_look / cam_up) are rebuilt each frame
 *     from vv_theta and vv_verta360 (bondviewMoveAnimationTick, :4325-4335),
 *     so setting those two angles reproduces the facing after one update.
 *   - registeredroom comes from the stan tile the player stands on, through
 *     bondviewUpdatePlayerRoom (bondview2.c:2731). The mark's room is the
 *     ORACLE for that, never hardcoded into the renderer.
 *
 * The whole apply is the spawn recipe with the mark's XZ+angles in place of a
 * start pad. It runs ONCE, after the level, player and stan geometry exist.
 */
#ifndef __sgi

#include <ultra64.h>
#include <bondgame.h>
#include <boss.h>
#include "bondview.h"
#include "player.h"
#include "stan.h"
#include "propobj.h"
#include "bgfog.h"

/* Declared here rather than by including bondview_internal.h: that header
 * pulls in HUD buffer-length macros this TU has no other reason to define.
 * The signature is bondview_internal.h:147 verbatim. */
extern void bondviewUpdatePlayerRoom(struct player *player);

extern int    fprintf(void *, const char *, ...);
extern void  *stderr;
extern char  *getenv(const char *);
extern double atof(const char *);
extern float  sinf(float);
extern float  cosf(float);

/* CAMERAMODE_FP ordinal (src/bondconstants.h enum) - the FP play camera. */
#define SL_CAMERAMODE_FP 4

/* Parsed once from the environment play.ps1 fills in. play.ps1 does all the
 * mark PARSING; this side receives already-split scalars, never an F8 report.
 * SL_TELEPORT is the on switch; absent means this whole file is a no-op beyond
 * one getenv on the first frame. */
static int   g_tp_checked;
static int   g_tp_on;
static int   g_tp_done;
static float g_tp_x, g_tp_y, g_tp_z, g_tp_theta, g_tp_verta;
static int   g_tp_room;         /* the room whose floor is looked for under the
                                 * XZ first; registeredroom is still computed
                                 * by the game from the tile actually chosen */
static int   g_tp_wait;         /* frames the player has existed before applying */

static void sl_teleport_check_env(void)
{
    const char *e;
    g_tp_checked = 1;
    e = getenv("SL_TELEPORT");
    g_tp_on = (e != NULL && *e != '\0' && *e != '0');
    if (!g_tp_on)
        return;
    e = getenv("SL_TELEPORT_X");     g_tp_x     = e ? (float) atof(e) : 0.0f;
    e = getenv("SL_TELEPORT_Y");     g_tp_y     = e ? (float) atof(e) : 0.0f;
    e = getenv("SL_TELEPORT_Z");     g_tp_z     = e ? (float) atof(e) : 0.0f;
    e = getenv("SL_TELEPORT_THETA"); g_tp_theta = e ? (float) atof(e) : 0.0f;
    e = getenv("SL_TELEPORT_VERTA"); g_tp_verta = e ? (float) atof(e) : 0.0f;
    e = getenv("SL_TELEPORT_ROOM");  g_tp_room  = e ? (int) atof(e) : -1;
}

/* Called once per pumped frame from the shim (src/platform/sl_ultra_shim.c).
 * Disabled -> a single getenv on frame 1 and an int test forever after, which
 * is the containment guarantee: an ordinary launch does no teleport work. */
void sl_teleport_poll(void)
{
    struct player *pl;
    struct StandTile *stan;
    struct StandTile *under = NULL;
    coord3d target;
    coord3d cand;
    coord3d probe;
    struct StandTile *walkstack[1];
    u8 roomlist[2];
    float under_y = 0.0f;
    const char *how;
    float stan_height;
    float look_rad;

    if (!g_tp_checked)
        sl_teleport_check_env();
    if (!g_tp_on || g_tp_done)
        return;

    pl = g_CurrentPlayer;
    if (pl == NULL || pl->prop == NULL)
        return;

    /* A short settle so the level, stan world and player are fully up before
     * the one apply. The stan lookup below is the real readiness gate - it
     * returns NULL until the geometry is loaded - but a few frames of margin
     * keeps the apply off the very first init frames. */
    if (g_tp_wait < 60) {
        g_tp_wait++;
        return;
    }

    /* THE MARK'S XZ locates Bond's floor tile. Y is seeded from the mark eye
     * but is not authoritative for the tile search - sub_GAME_7F0AFB78 works
     * in XZ - so a NULL here means the geometry is not ready yet, not that the
     * spot is invalid. */
    target.f[0] = g_tp_x;
    target.f[1] = g_tp_y;
    target.f[2] = g_tp_z;
    stan = sub_GAME_7F0AFB78(&target.f[0], &target.f[1], &target.f[2], 30.0f);
    if (stan == NULL)
        return;
    how = "nearest";
    /* target now holds the candidate point ON that tile (a corner nudged 10%
     * toward the centre, or the midpoint) - the walk below starts from it. */
    cand = target;

    /* THE TILE UNDER THE POINT, not the tile with the nearest corner.
     * sub_GAME_7F0AFB78 (stan.c:795) ranks every tile by the 3D distance from
     * the request to three near-corner points and the midpoint; it never tests
     * containment. On a yard built from very large tiles (Depot, 2026-09-15:
     * 12 of 19 stops) a point in the middle of its own tile is farther from
     * those four candidates than from a small tile of the warehouse next door,
     * so Bond registered the warehouse while standing outside it and the
     * yard's own room was not in the visible set. Two of the game's own
     * primitives settle it, in this order:
     *   1. stanFindTileBelowPos (stan.c:342) - the highest floor tile under a
     *      point, restricted to a room list. Its containment test is the
     *      tile's reference TRIANGLE (the three tail.half indices, stan.c:443)
     *      rather than the whole polygon, so it can miss a point inside a
     *      large quad; when it hits, it also resolves multi-level rooms (the
     *      probe sits 120 above the requested Y: above a floor named by a room
     *      bound, below a mezzanine ~270 up).
     *   2. walkTilesBetweenPoints_NoCallback (stan.c:1469, the game's own
     *      tile-to-tile movement) from the nearest tile's candidate point to
     *      the requested XZ: it crosses linked edges only, so it ends on the
     *      tile that really contains the point, or fails at a wall or an
     *      unlinked edge - in which case the point is not connected floor.
     * Then the any-room containment, then the nearest-corner tile as the
     * last resort. The diagnostic names which one won. */
    probe.f[0] = g_tp_x;
    probe.f[1] = g_tp_y + 120.0f;
    probe.f[2] = g_tp_z;
    if (g_tp_room >= 0 && g_tp_room < 0xff) {
        roomlist[0] = (u8) g_tp_room;
        roomlist[1] = 0xff;
        under = stanFindTileBelowPos(&probe, roomlist, &under_y);
        if (under != NULL)
            how = "under-point-in-room";
    }
    if (under == NULL) {
        walkstack[0] = stan;
        if (walkTilesBetweenPoints_NoCallback(walkstack, cand.f[0], cand.f[2],
                                              g_tp_x, g_tp_z)
            && walkstack[0] != NULL) {
            under = walkstack[0];
            how = "walked-from-nearest";
        }
    }
    if (under == NULL) {
        under = stanFindTileBelowPos(&probe, NULL, &under_y);
        if (under != NULL)
            how = "under-point-any-room";
    }
    if (under != NULL)
        stan = under;

    /* Restore the ORIGINAL mark XZ - the lookup nudges target toward the tile
     * centre, and the mark's own XZ is the authority for where Bond stands. */
    target.f[0] = g_tp_x;
    target.f[2] = g_tp_z;

    /* End the intro cleanly through the game's own transition, exactly as the
     * intro does when it finishes (bondview2.c:847). This equips weapons and
     * drops the player into FP; the marked view was taken in FP (camera-mode
     * 0). No-op if we are already there. */
    if (g_CameraMode != SL_CAMERAMODE_FP) {
        bondviewSetCameraMode(SL_CAMERAMODE_FP);
        /* THE PLAY ENVIRONMENT, not the intro's. The game loads a level's fog
         * row twice on the way into FP: CAMERAMODE_INTRO loads the CINEMA row
         * (fogLoadLevelEnvironment(stage, 1), bondview2.c:780) and the SWIRL
         * step that follows reloads the play row (arg 0, bondview2.c:812);
         * entering FP with one player loads nothing (bondview2.c:863). Jumping
         * INTRO -> FP directly therefore left the cinema row in force for the
         * whole teleported run wherever a cinema row exists - Dam and Surface
         * 2 (bgfog.c fog_tables, id+900): Surface 2 played its sweeps under
         * farfog 8000 / nearfog 6000 / maxvis 8000 instead of the play row's
         * 2000 / 2500 / 3055, so the owner's live run (events: zfar 8000 ->
         * 2000, far-fog 40000 -> 10000 at the swirl) and every teleported
         * capture disagreed on the fog of every frame. This is the SWIRL
         * step's own call, so the teleported run consumes exactly the row a
         * played run does. Levels without a cinema row resolve to the same
         * row either way (bgfog.c:480-509) - a no-op there. */
        fogLoadLevelEnvironment(bossGetStageNum(), 0);
    }

    /* The floor height at the mark's XZ, and the standing eye that sits above
     * it. eyeheight is the game's own per-frame value; the stable eye Y after
     * this is field_70 + eyeheight, so field_70 = stan_height puts Bond on the
     * floor and the eye at its natural standing height there. */
    stan_height = bondviewYPositionRelated(stan, target.f[0], target.f[2]);
    target.f[1] = pl->eyeheight + stan_height;

    pl->vv_theta = g_tp_theta;
    pl->vv_verta = g_tp_verta;
    pl->stanHeight = stan_height;
    pl->field_70 = stan_height;

    /* The game's own place-Bond primitive - the same call the spawn path uses.
     * It sets collision_position, current_tile_ptr (and _for_portals), pos and
     * pos3 from target, and the tile. */
    change_player_pos_to_target(&pl->field_488, &target, stan);

    /* theta_transform is rebuilt from vv_theta every frame by
     * bondviewApplyVertaTheta; set it here too so the very first post-teleport
     * frame is already consistent, matching the spawn recipe. */
    look_rad = g_tp_theta * (6.2831855f / 360.0f);
    pl->field_488.theta_transform.f[0] = -sinf(look_rad);
    pl->field_488.theta_transform.f[1] = 0.0f;
    pl->field_488.theta_transform.f[2] = cosf(look_rad);

    pl->prop->pos.f[0] = pl->bondprevpos.f[0] = target.f[0];
    pl->prop->pos.f[1] = pl->bondprevpos.f[1] = target.f[1];
    pl->prop->pos.f[2] = pl->bondprevpos.f[2] = target.f[2];
    pl->prop->stan = stan;

    /* Register the room FROM the new tile through normal game logic; portals
     * and visibility are then computed from here on the next traversal. The
     * mark's room is the oracle we print against, not an input to this. */
    bondviewUpdatePlayerRoom(pl);

    g_tp_done = 1;

    fprintf(stderr,
        "sl_teleport: requested pos=%.3f %.3f %.3f theta=%.3f verta=%.3f "
        "room=%d\n",
        (double) g_tp_x, (double) g_tp_y, (double) g_tp_z,
        (double) g_tp_theta, (double) g_tp_verta, g_tp_room);
    fprintf(stderr,
        "sl_teleport: applied  floor=%.3f eyeheight=%.3f "
        "registeredroom=%d tile=%s\n",
        (double) stan_height, (double) pl->eyeheight,
        (int) pl->registeredroom, how);
}

#endif /* __sgi */
