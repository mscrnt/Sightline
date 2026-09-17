/**
 * Small read-only windows into game state, for the platform layer.
 *
 * Why this file exists. src/platform compiles against the HOST headers alone -
 * the N64 SDK include tree shadows them (its stdarg.h has no __gnuc_va_list),
 * so tools/native/build.sh gives src/platform and src/gfx a different command
 * line from everything else. That means the input layer physically cannot see
 * a game struct, and for a while it coped by GUESSING what the game was doing
 * and synthesising inputs to match.
 *
 * Guessing produced three wrong control mappings in a row, each of which
 * reached the player as "the controls are broken": forward on the wrong key,
 * look welded to movement, left and right reversed. The game knows its own
 * control style. It should be asked, not modelled.
 *
 * src/native IS compiled with the game include path, so it can read a struct
 * and hand back a plain integer. Keep everything here to that shape: read one
 * value, return a scalar, no writes, no pointers escaping. The layering rule
 * runs the other way (src/game must not reach into src/platform) and is
 * unaffected - nothing here is included by the game.
 */
#ifndef __sgi

#include <ultra64.h>
#include <bondgame.h>
#include <boss.h>
#include "bondview.h"
#include "player.h"
#include "joy.h"
#include "options.h"
#include "fr.h"
#include "propobj.h"
#include "bg.h"
#include <math.h>

/**
 * The live control style for a player, as a CONTROLLER_CONFIG ordinal
 * (src/bondconstants.h: 0 = 1.1 Honey, 1 = 1.2 Solitaire, ...).
 *
 * Returns -1 before the player exists, which is the common case during boot -
 * callers must treat that as "not known yet" and not as a style.
 */
s32 sl_game_control_style(s32 player_num)
{
    if (player_num < 0 || player_num >= 4)
        return -1;
    if (g_playerPointers[player_num] == NULL)
        return -1;
    return (s32) g_playerPointers[player_num]->cur_player_control_type_0;
}

/**
 * Whether a player is currently in aim mode (the crosshair sight), 0 or 1,
 * or -1 before the player exists.
 *
 * The input layer needs this because aim mode changes which PHYSICAL AXIS the
 * game reads. bondviewProcessInput clears every can* flag while
 * insightaimmode is set and reads the raw stick instead (bondview2.c:5252 -
 * stick_y past +/-60 drives speedVerta, stick_x past +/-60 drives aimTurn).
 * Under a 1.1-like style that leaves the C-button pitch pair dead for exactly
 * as long as the player is aiming, so an input layer that encoded pitch to C
 * unconditionally would go silent at the moment aiming matters most.
 *
 * One frame stale by construction: insightaimmode is assigned inside
 * bondviewProcessInput from the buttons it was handed, so a caller sampling
 * before the tick sees the previous tick's value. Input is latched per frame
 * anyway, so this costs a frame of latency on the mode switch and nothing else.
 */
s32 sl_game_aim_mode(s32 player_num)
{
    if (player_num < 0 || player_num >= 4)
        return -1;
    if (g_playerPointers[player_num] == NULL)
        return -1;
    return g_playerPointers[player_num]->insightaimmode != 0;
}

/**
 * Set a player's control style. Returns 0 on success, -1 if there is no player.
 *
 * Needed because DIRECT BOOT never restores it. Measured: a 1200-frame run that
 * boots straight into a level performs ZERO osEepromRead and ZERO osEepromWrite
 * calls. The normal flow reaches fileLoadSettingsForFolder (file2.c:1283) via
 * file.c:68, and that is what applies the saved control type, invert-look,
 * autoaim and the rest; SL_BOOT_LEVEL jumps past it. So the style is neither
 * loaded at start nor written at exit, initBONDdataforPlayer forces
 * CONTROLLER_CONFIG_HONEY (player.c:502), and whatever the player picks in the
 * options menu is gone the moment the process ends.
 *
 * This is deliberately NOT a reimplementation of Rare's save format. It sets
 * the same value through the game's own setter (options.c:454), and the
 * platform layer remembers it across runs in a sidecar. Rule 5 is satisfied
 * because nothing about the game's behaviour changes - the player's own choice
 * is simply still there next time.
 */
s32 sl_game_set_control_style(s32 player_num, s32 style)
{
    if (player_num < 0 || player_num >= 4)
        return -1;
    if (g_playerPointers[player_num] == NULL)
        return -1;
    if (style < 0)
        return -1;
    cur_player_set_control_type((int) style);
    return 0;
}

/**
 * How many controllers the GAME believes are attached.
 *
 * Not a restatement of what the shim reports. joyCheckStatus (src/joy.c:203)
 * calls osContInit exactly once and rebuilds g_ConnectedControllers from
 * osContGetQuery on every later call, and joyGetStickX/joyGetButtons refuse to
 * read a pad whose bit is clear (:283, :312) - so what the platform intends and
 * what the game acts on are two different facts, and only this one decides
 * whether the 2.x styles work.
 *
 * It is also the measurement the second controller needs: presenting two pads
 * changes joyGetControllerCount, and that value is read by the front end
 * (front.c:2797, :4352) and the watch menu (options.c:1396). Reporting it makes
 * those consequences observable instead of assumed.
 */
s32 sl_game_controller_count(void)
{
    return (s32) joyGetControllerCount();
}

/**
 * Which INPUT MODE the game is in, as a small ordinal:
 *
 *   0  gameplay - the player is on foot with the watch closed
 *   1  the watch is open (GoldenEye's in-level pause and options menu)
 *   2  the front end - the title screen, or before a player exists
 *
 * Read-only and derived from two signals, both of which are the game's own:
 *
 *   g_StageNum vs LEVELID_TITLE (90)     src/boss.c:110, accessor at :724,
 *                                        the constant at src/bondconstants.h:1726
 *   watch_animation_state != WATCH_ANIMATION_0x0
 *                                        src/bondconstants.h:2876 - "watch
 *                                        closed, normal play" is the 0 case
 *
 * The platform layer needs this for three things and nothing else: whether to
 * hold the pointer grab, whether to discard mouse motion, and whether Escape
 * should read as N64 START (close the watch / open the pause menu) or as N64 B
 * (the front end's own cancel - front.c:2315, :2403, :3240). It is NOT a hook
 * for the mouse POINTER: that is a separate question with a separate answer
 * (sl_game_pointer_menu_active, src/native/sl_menu_pointer.c), because "release
 * the grab" is true of the watch too and the watch has no cursor for a pointer
 * to move. The keyboard and the wheel still drive the STICK, which is what both
 * menu systems already read (front.c:1148 frontUpdateControlStickPosition,
 * options.c:566-642), and no menu code changes for either.
 *
 * The player-pointer check has to come before the watch check: on the title
 * screen there is no player to ask, and reading through a NULL pointer to find
 * out whether a menu is up would be its own kind of funny.
 */
s32 sl_game_menu_mode(void)
{
    if (bossGetStageNum() == LEVELID_TITLE)
        return 2;
    if (g_playerPointers[0] == NULL)
        return 2;
    if (g_playerPointers[0]->watch_animation_state != WATCH_ANIMATION_0x0)
        return 1;
    return 0;
}


/**
 * Whether the player's Look Up/Down option is set to UPRIGHT, 0 or 1.
 *
 * The input layer needs this for ONE narrow purpose: the mouse must have
 * conventional PC pitch whatever this option says, while the PAD must keep
 * obeying it. Owner decision, 2026-09-01 - the option is a CONTROLLER setting
 * and the mouse is a separate producer.
 *
 * The option reaches pitch through bondviewProcessInput:
 *   :4849  moveData.invertPitch = get_cur_player_look_vertical_inverted() == 0;
 *   :5579  if (moveData.invertPitch == 0) { negate controlStickYRaw and
 *          analogPitch, swap speedVertaDown/Up }
 * so the block runs when the getter returns NON-ZERO. options.c:133 declares
 * the option's labels in the order "reverse, upright" with a default
 * current_value of 0, and options.c:498 returns current_value unchanged, so
 * non-zero is UPRIGHT and 0 is REVERSE - GoldenEye's authentic default, under
 * which the block does NOT run.
 *
 * game_options_entries is a file-scope array that exists before any player
 * does, so unlike the other queries here this one is always answerable.
 */
s32 sl_game_look_upright(void)
{
    return get_cur_player_look_vertical_inverted() != 0;
}



/* ---- B-100 PROBE: the intro camera path and what it passes through -------
 *
 * TEMPORARY, native-only, inert unless SL_CAM_DBG is set. Prints, once per
 * pumped frame, the state the intro camera is a function of - camera mode,
 * camera world position, and the projection's near/far planes (fr.c's
 * VideoSettings, which bgfog.c:301 loads from the level's environment row) -
 * plus every prop within SL_CAM_NEAR units of the camera.
 *
 * The point is to make the camera PATH and the projection separately
 * observable, because B-100's two candidate causes ("the camera goes deeper"
 * and "the near plane clips less") predict different things about which of
 * these two lines moves. The same quantities are readable from the cartridge
 * out of RDRAM at the matching build's symbols, so the two can be diffed.
 *
 * ---- `pos=` IS AN INTRO FIELD AND IS FROZEN IN GAMEPLAY -------------------
 *
 * MEASURED 2026-09-08 on the owner's marks run
 * (20260908-205947-lvl33/log): 6202 `sl_cam: f... pos=` lines carry 133
 * DISTINCT positions, all of them from the intro. From frame 531 - the frame
 * cameramode drops to 0 - to the end of the 12255-frame session, `pos=`
 * reports 16659.31,480.98,19463.86 UNCHANGED, including at all eight frames
 * the owner marked, whose eyes are hundreds of units apart. Reading this line
 * to locate a player produces a confident, wrong answer.
 *
 * The `sl_eye:` line below therefore prints the field the F8 record itself
 * names as authoritative - viewtoworldmtxf m[3] (bondview.c:849) - so a live
 * run and a mark can be compared on the same quantity. `pos=` is kept because
 * B-100 is about the intro, where it IS the camera.
 */
static s32 sl_mark_player_room_safe(struct player *pl);

s32 sl_cam_probe_frame(s32 frame)
{
    extern int fprintf(void *, const char *, ...);
    extern void *stderr;
    extern VideoSettings *g_ViBackData;
    static int on = -1, nearlim = 0, every = 1, objonly = 0;
    static unsigned calls = 0;
    struct player *pl;
    PropRecord *pr;
    s32 n;

    if (on < 0) {
        const char *e = getenv("SL_CAM_DBG");
        on = (e != NULL && *e != '\0' && *e != '0');
        e = getenv("SL_CAM_NEAR");
        nearlim = (e != NULL) ? atoi(e) : 400;
        e = getenv("SL_CAM_EVERY");
        every = (e != NULL && atoi(e) > 0) ? atoi(e) : 1;
        e = getenv("SL_CAM_OBJONLY");
        objonly = (e != NULL && *e != '0');
    }
    /* THE STRIDE COUNTS THIS PROBE'S OWN CALLS, not the frame number.
     *
     * MEASURED 2026-09-08. It used to read `(frame % every) != 0`, and the
     * caller (src/platform/sl_ultra_shim.c:1081) only ever reaches here on
     * ODD values of sl_frame - one call per game frame against a counter that
     * advances twice. So every EVEN stride selected nothing at all:
     * SL_CAM_EVERY=100 produced ZERO lines over 3000 pumped frames where the
     * same run with the stride unset produced 1499. A probe whose stride can
     * never fire reads as "the code never ran", which is the exact silent
     * failure this project has now recorded nine times. */
    if (!on)
        return 0;
    if ((calls++ % every) != 0)
        return 0;
    pl = g_CurrentPlayer;
    if (pl == NULL)
        return 0;

    fprintf(stderr, "sl_cam: f%d mode=%d pos=%.2f,%.2f,%.2f "
                    "near=%.2f far=%.2f fovy=%.3f aspect=%.5f\n",
            (int) frame, (int) pl->cameramode,
            (double) pl->pos.f[0], (double) pl->pos.f[1], (double) pl->pos.f[2],
            (double) g_ViBackData->znear, (double) g_ViBackData->zfar,
            (double) g_ViBackData->fovy, (double) g_ViBackData->aspect);
    /* TEMPORARY: the EYE, from the same authority the F8 mark cites
     * (viewtoworldmtxf m[3]). pl->pos above is a cutscene-camera field and is
     * FROZEN during gameplay - measured constant across 6000 gameplay frames
     * of the owner's marks run - so it cannot be used to locate a replay. */
    if (pl->viewtoworldmtxf != NULL)
        fprintf(stderr, "sl_eye: f%d %.3f %.3f %.3f room=%d\n", (int) frame,
                (double) pl->viewtoworldmtxf->m[3][0],
                (double) pl->viewtoworldmtxf->m[3][1],
                (double) pl->viewtoworldmtxf->m[3][2],
                (int) sl_mark_player_room_safe(pl));

    n = 0;
    for (pr = g_ActivePropsTail; pr != NULL && n < 4096; pr = pr->prev, n++) {
        f32 dx = pr->pos.f[0] - pl->pos.f[0];
        f32 dy = pr->pos.f[1] - pl->pos.f[1];
        f32 dz = pr->pos.f[2] - pl->pos.f[2];
        f32 d2 = dx * dx + dy * dy + dz * dz;
        if (d2 > (f32) nearlim * (f32) nearlim)
            continue;
        if (objonly && pr->type != PROP_TYPE_OBJ)
            continue;
        fprintf(stderr, "sl_cam:   f%d prop type=%d obj=%d pos=%.2f,%.2f,%.2f"
                        " dist=%.2f zdepth=%.2f\n",
                (int) frame, (int) pr->type,
                (pr->type == PROP_TYPE_OBJ && pr->obj != NULL)
                    ? (int) ((ObjectRecord *) pr->obj)->obj : -1,
                (double) pr->pos.f[0], (double) pr->pos.f[1],
                (double) pr->pos.f[2], (double) sqrtf(d2),
                (double) pr->zDepth);
    }
    return 1;
}



/* ---- LIVE OWNER BUG MARK: the game's half of the record ------------------
 *
 * F8 writes one file describing the moment the owner was looking at a defect.
 * The renderer supplies what it drew (sl_mark_render in src/gfx/sl_gfx_dl.c);
 * this supplies what the SIMULATION believed - camera, rooms, projection -
 * because src/platform physically cannot see a game struct (see the header of
 * this file) and src/gfx must not.
 *
 * READ-ONLY, like everything else here. No writes, no pointers escaping, one
 * text buffer out. It runs once per mark, never per frame.
 *
 * THE ROOM NAMING IS THE POINT. The renderer records the depth-1 G_DL operand
 * a triangle came from and has no idea what a room is. bgRenderRoomPrimary
 * branches with OS_K0_TO_PHYSICAL(g_BgRoomInfo[room].ptr_expanded_mapping_info)
 * (src/game/bg.c:2947) and bgRenderRoomSecondary with the secondary pointer
 * (:3000), so the operand IS the room's identity - it just needs the table
 * only this side can read. Nothing in src/game changes to make that work.
 */
/* THE PLAYER'S ROOM, WITHOUT FAULTING WHEN THERE IS NO LEVEL.
 *
 * MEASURED CRASH, 2026-09-08. Pressing F8 on the boot/title screens took an
 * ACCESS VIOLATION reading address 0x00000003, resolved to
 * bondviewGetCurrentPlayersRoom+0x34 called from sl_mark_game_render. The
 * accessor (bondview2.c:9800) reads
 * g_CurrentPlayer->field_488.current_tile_ptr_for_portals->room with no null
 * check, and outside a level that pointer is null - 0x3 is `room`'s offset
 * within StandTile. g_CurrentPlayer itself is NON-null there, so the existing
 * `pl == NULL` guard never fired. The mark wrote its .bmp and then died before
 * its .txt, which is the worst possible failure for a facility whose entire
 * job is to be trustworthy when the owner presses one key.
 *
 * THE GAME FUNCTION IS NOT TOUCHED (rule 5): faulting there is Rare's
 * behaviour and every in-level caller satisfies its preconditions. The guard
 * belongs to the READER, so this restates the accessor's own two branches and
 * checks the pointer each one is about to follow. Returns -1 for "no tile to
 * ask", which the record prints as UNKNOWN rather than as a room number. */
static s32 sl_mark_player_room_safe(struct player *pl)
{
    if (pl == NULL)
        return -1;
    if (pl->cameramode == 1 && pl->cameratile != NULL)
        return (s32) pl->cameratile->room;
    if (pl->field_488.current_tile_ptr_for_portals == NULL)
        return -1;
    return (s32) pl->field_488.current_tile_ptr_for_portals->room;
}

static u32 sl_mark_dl_room(u32 addr, const char **kind)
{
    s32 i;
    *kind = "";
    if (addr == 0) return (u32) -1;
    for (i = 0; i < g_MaxNumRooms; i++) {
        void *p = g_BgRoomInfo[i].ptr_expanded_mapping_info;
        void *q = g_BgRoomInfo[i].ptr_secondary_expanded_mapping_info;
        if (p != NULL && (u32) OS_K0_TO_PHYSICAL(p) == addr) {
            *kind = "primary";
            return (u32) i;
        }
        if (q != NULL && (u32) OS_K0_TO_PHYSICAL(q) == addr) {
            *kind = "secondary";
            return (u32) i;
        }
    }
    return (u32) -1;
}

/* Resolve one depth-1 list operand to a room, for the platform layer's mark
 * writer. Returns the room number, or -1 when the operand belongs to no room
 * (characters, props, the view model and the 2D passes all branch from lists
 * that are not in g_BgRoomInfo). kindout receives 0 for none, 1 for a room's
 * primary geometry, 2 for its secondary (transparent) geometry. */
s32 sl_mark_room_of_dl(u32 addr, s32 *kindout)
{
    const char *k;
    u32 r = sl_mark_dl_room(addr, &k);
    if (kindout != NULL)
        *kindout = (r == (u32) -1) ? 0 : (k[0] == 'p' ? 1 : 2);
    return (r == (u32) -1) ? -1 : (s32) r;
}

/* THE CRASH REPORT'S POSE. B-139.
 *
 * An owner crash used to arrive as addresses and a frame number and nothing
 * that said WHERE in the level it happened - the Aztec crash of 2026-09-16
 * (run 20260916-074840-lvl28) came with a 13 KB log, an input recording that
 * does not reproduce the session, and no position at all. This writes the
 * same [teleport] section the F8 mark writes, so
 * `play.ps1 -Level <name> -TeleportMark <run>\crash.txt` puts the camera
 * where the crash was.
 *
 * Called from inside the fault handler, so every pointer is checked with
 * the caller's readability predicate before it is followed (NULL predicate
 * = trust the pointers, for a platform without one). The eye is
 * viewtoworldmtxf m[3], the authority the mark cites; the room is the same
 * two-branch accessor the mark uses. READ-ONLY, bounded, no allocation. */
s32 sl_crash_pose(char *out, s32 n, int (*readable)(const void *, unsigned long))
{
    extern int snprintf(char *, unsigned int, const char *, ...);
    struct player *pl;
    Mtxf *v2w;
    s32 at = 0;

    if (out == NULL || n < 64) return 0;
    out[0] = '\0';
#define PCAT(...)  do {                                                       \
        int k_;                                                               \
        if (at >= n - 1) break;                                               \
        k_ = snprintf(out + at, (unsigned int) (n - at), __VA_ARGS__);         \
        if (k_ > 0) at += k_;                                                 \
        if (at > n - 1) at = n - 1;                                            \
    } while (0)
#define POK(p, len) ((p) != NULL && (readable == NULL || readable((p), (len))))

    PCAT("\n[teleport]\n");
    PCAT("crash-stage         %d\n", (int) bossGetStageNum());
    pl = g_CurrentPlayer;
    if (!POK(pl, sizeof *pl)) {
        PCAT("teleport-pos        UNKNOWN - no readable current player\n");
        return at;
    }
    v2w = pl->viewtoworldmtxf;
    if (!POK(v2w, sizeof *v2w)) {
        PCAT("teleport-pos        UNKNOWN - viewtoworldmtxf not readable\n");
        return at;
    }
    {
        s32 room = -1;
        if (pl->cameramode == 1) {
            if (POK(pl->cameratile, sizeof *pl->cameratile))
                room = (s32) pl->cameratile->room;
        } else if (POK(pl->field_488.current_tile_ptr_for_portals,
                       sizeof *pl->field_488.current_tile_ptr_for_portals)) {
            room = (s32) pl->field_488.current_tile_ptr_for_portals->room;
        }
        PCAT("teleport-pos        %.3f %.3f %.3f\n"
             "teleport-theta      %.3f\n"
             "teleport-verta      %.3f\n"
             "teleport-room       %d\n"
             "crash-camera-mode   %d\n"
             "crash-tile-room     %d\n"
             "teleport-command    play.ps1 -Level <name> -TeleportMark <this file>\n",
             (double) v2w->m[3][0], (double) v2w->m[3][1], (double) v2w->m[3][2],
             (double) pl->vv_theta, (double) pl->vv_verta,
             (int) pl->registeredroom, (int) pl->cameramode, (int) room);
    }
#undef POK
#undef PCAT
    return at;
}

s32 sl_mark_game_render(char *out, s32 n)
{
    extern int snprintf(char *, unsigned int, const char *, ...);
    extern VideoSettings *g_ViBackData;
    struct player *pl;
    Mtxf *v2w;
    s32 at = 0, i, cnt;
    s32 rooms[64];

    if (out == NULL || n < 64) return 0;
    out[0] = '\0';

#define MCAT(...)  do {                                                       \
        int k_;                                                               \
        if (at >= n - 1) break;                                               \
        k_ = snprintf(out + at, (unsigned int) (n - at), __VA_ARGS__);         \
        if (k_ > 0) at += k_;                                                 \
        if (at > n - 1) at = n - 1;                                            \
    } while (0)

    pl = g_CurrentPlayer;
    if (pl == NULL) {
        MCAT("[camera]\nNO CURRENT PLAYER - the mark was taken outside a level.\n"
             "  Nothing below this line would be a position; none is invented.\n"
             "\n[projection]\nunavailable without a player.\n"
             "\n[rooms]\nunavailable without a player.\n");
        return at;
    }

    /* ---- [camera] --------------------------------------------------------
     *
     * ONE AUTHORITY, NAMED. viewtoworldmtxf is the transform the renderer was
     * actually handed, and its translation row is the eye in WORLD units -
     * established, not assumed: bondview.c:967-1002 builds every frustum
     * plane offset as a dot product of a basis row with m[3][], and
     * bondview.c:1092 then tests that offset against PROP WORLD POSITIONS.
     * A plane test only closes if both operands are in the same space, so
     * m[3] is world.
     *
     * WHAT WAS REMOVED AND WHY. This record used to print a field called
     * `camera-world-pos` taken from `pl->pos`, whose declaration in
     * bondview.h:305 is itself a guess ("canonical memcampos ?"). Measured
     * across three marks in one run it was byte-identical - 16011.993
     * 613.779 19719.410 every time - while the player plainly moved and this
     * matrix's translation row tracked the movement correctly on each mark.
     * A confidently-named stale value is worse than no value, so the field is
     * gone rather than renamed: nothing here now claims to be the camera
     * except the matrix that demonstrably is.
     */
    MCAT("[camera]\n"
         "authority           viewtoworldmtxf (bondview.c:849). Its translation\n"
         "                    row IS the eye, in world units - every frustum\n"
         "                    plane offset at bondview.c:967-1002 dots a basis\n"
         "                    row with it and bondview.c:1092 compares that to\n"
         "                    prop WORLD positions. There is no second position\n"
         "                    field in this record to disagree with it.\n"
         "camera-mode         %d\n",
         (int) pl->cameramode);

    v2w = pl->viewtoworldmtxf;
    if (v2w != NULL) {
        MCAT("eye-world-pos       %.3f %.3f %.3f   (viewtoworldmtxf m[3])\n"
             "basis-right         %.5f %.5f %.5f   (m[0])\n"
             "basis-up            %.5f %.5f %.5f   (m[1])\n"
             "basis-back          %.5f %.5f %.5f   (m[2]; forward is its negation)\n"
             "forward-from-mtxf   %.5f %.5f %.5f\n",
             (double) v2w->m[3][0], (double) v2w->m[3][1], (double) v2w->m[3][2],
             (double) v2w->m[0][0], (double) v2w->m[0][1], (double) v2w->m[0][2],
             (double) v2w->m[1][0], (double) v2w->m[1][1], (double) v2w->m[1][2],
             (double) v2w->m[2][0], (double) v2w->m[2][1], (double) v2w->m[2][2],
             -(double) v2w->m[2][0], -(double) v2w->m[2][1], -(double) v2w->m[2][2]);
        MCAT("viewtoworld-mtxf    %.5f %.5f %.5f %.5f\n"
             "                    %.5f %.5f %.5f %.5f\n"
             "                    %.5f %.5f %.5f %.5f\n"
             "                    %.5f %.5f %.5f %.5f\n",
             (double) v2w->m[0][0], (double) v2w->m[0][1],
             (double) v2w->m[0][2], (double) v2w->m[0][3],
             (double) v2w->m[1][0], (double) v2w->m[1][1],
             (double) v2w->m[1][2], (double) v2w->m[1][3],
             (double) v2w->m[2][0], (double) v2w->m[2][1],
             (double) v2w->m[2][2], (double) v2w->m[2][3],
             (double) v2w->m[3][0], (double) v2w->m[3][1],
             (double) v2w->m[3][2], (double) v2w->m[3][3]);
        /* WORLD-TO-VIEW, by inverting the rigid transform rather than reading
         * a second field that might be a different frame's. R is orthonormal
         * for a camera basis, so the inverse is R^T and -R^T t.
         *
         * THE VALIDITY CHECK IS PRINTED, not assumed: each basis row's dot
         * product with itself is 1.0 exactly when the row is a unit vector,
         * and if any of the three is not 1.0 the inversion above is wrong and
         * the reader can see that it is wrong.
         *
         * SQUARED LENGTHS, DELIBERATELY - and this is a trap worth recording.
         * The first version took a square root and printed 30680096.000000 for
         * a row whose three components were right there on the line above and
         * plainly unit length. Cause: include/math.h declares sqrtf(float) to
         * GCC but NOT sqrt(double) - `gcc -E -P -Iinclude` over a file calling
         * both emits the sqrtf prototype and no sqrt prototype - so sqrt() in
         * any translation unit built without __sgi is an implicit declaration
         * returning int, and the value it yields is nonsense. A tree-wide
         * `grep -rn "[^a-zA-Z_]sqrt(" src/ --include=*.c --include=*.h` finds
         * exactly one other occurrence and it is inside a comment, so nothing
         * that shipped was affected - but the next caller would be. The dot
         * product needs no library call and is exact. */
        {
            double r[3][3], t[3], d2[3];
            int a, b;
            for (a = 0; a < 3; a++) {
                for (b = 0; b < 3; b++) r[a][b] = (double) v2w->m[a][b];
                d2[a] = r[a][0]*r[a][0] + r[a][1]*r[a][1] + r[a][2]*r[a][2];
            }
            for (a = 0; a < 3; a++)
                t[a] = -( (double) v2w->m[3][0] * r[a][0]
                        + (double) v2w->m[3][1] * r[a][1]
                        + (double) v2w->m[3][2] * r[a][2]);
            MCAT("worldtoview-derived %.5f %.5f %.5f %.5f\n"
                 "  (R^T, -R^T t)     %.5f %.5f %.5f %.5f\n"
                 "                    %.5f %.5f %.5f %.5f\n"
                 "                    %.5f %.5f %.5f %.5f\n"
                 "basis-row-dots      %.6f %.6f %.6f  (row . itself; 1.0 each,\n"
                 "                    or the inversion above is NOT valid)\n",
                 r[0][0], r[1][0], r[2][0], 0.0,
                 r[0][1], r[1][1], r[2][1], 0.0,
                 r[0][2], r[1][2], r[2][2], 0.0,
                 t[0], t[1], t[2], 1.0,
                 d2[0], d2[1], d2[2]);
        }
    } else {
        MCAT("eye-world-pos       UNKNOWN - viewtoworldmtxf is NULL, and this\n"
             "                    record has no second source for it.\n");
    }

    /* The game's own look angles, kept beside the matrix on purpose. They are
     * a SECOND, INDEPENDENT source for orientation only - never for position.
     * If the derived forward here and forward-from-mtxf above ever disagree,
     * that disagreement is itself the finding. */
    MCAT("look-theta-deg      %.3f\n"
         "look-verta-deg      %.3f  (verta360 %.3f)\n",
         (double) pl->vv_theta, (double) pl->vv_verta,
         (double) pl->vv_verta360);
    {
        double th = (double) pl->vv_theta * 3.14159265358979 / 180.0;
        double vt = (double) pl->vv_verta360 * 3.14159265358979 / 180.0;
        double cv = cos(vt);
        MCAT("forward-from-angles %.5f %.5f %.5f  (independent of the matrix;\n"
             "                    a disagreement with forward-from-mtxf is\n"
             "                    itself a finding, not noise)\n",
             -sin(th) * cv, sin(vt), -cos(th) * cv);
    }

    /* RENDER-SPACE ORIGIN, under its real name.
     *
     * This record used to print current_model_pos as `player-model-pos`. It is
     * not the player and never was: bondview2.c:8380 assigns it from
     * getRoomPositionScaledByIndex(room), i.e. the ORIGIN of the player's
     * current room, scaled; bondview2.c:8381 then derives current_room_pos
     * from it by get_room_data_float1(). bgroomtrans.c:216 subtracts it from
     * each room's own scaled origin to build that room's transform, which is
     * what it is for - it is the offset world geometry is rendered relative
     * to. Measurement agreed: the value was exactly current_room_pos * 4.28008
     * on the frame it was checked, which is the scale factor and not a
     * coincidence. Named for what it is now. */
    MCAT("render-origin       %.3f %.3f %.3f  (current_model_pos: the scaled\n"
         "                    ORIGIN of the player's room, bondview2.c:8380 -\n"
         "                    the offset rooms are drawn relative to. NOT a\n"
         "                    player or camera position.)\n"
         "render-origin-unsc  %.3f %.3f %.3f  (current_room_pos, the same point\n"
         "                    through get_room_data_float1, bondview2.c:8381)\n",
         (double) pl->current_model_pos.f[0],
         (double) pl->current_model_pos.f[1],
         (double) pl->current_model_pos.f[2],
         (double) pl->current_room_pos.f[0],
         (double) pl->current_room_pos.f[1],
         (double) pl->current_room_pos.f[2]);

    /* ---- [teleport] ------------------------------------------------------
     *
     * The load-bearing viewpoint values, and ONLY those, in a compact
     * machine-parseable form so tools/windows/play.ps1 -TeleportMark can read
     * them without parsing the [camera] prose above. This is a
     * LOCATION/VIEWPOINT anchor - no AI, objectives, inventory, scripts, RNG
     * or player-struct dumps belong here.
     *
     * pos is the FP camera eye (viewtoworldmtxf m[3]); in FP that IS
     * field_488.pos, which the teleport reconstructs by standing Bond on the
     * stan floor at this XZ (src/native/sl_teleport.c explains the dataflow).
     * theta/verta are the game's own look angles, the authority for facing.
     * room is registeredroom, the ORACLE the teleport validates against - not
     * an input to it. eye-world-pos stays in [camera] as independent
     * validation; this section does not replace it.
     *
     * A legacy mark predating this section is still teleportable: play.ps1
     * falls back to [camera] eye-world-pos + look-theta/verta + [rooms]
     * player-room, which carry the same quantities. */
    if (v2w != NULL) {
        MCAT("\n[teleport]\n"
             "teleport-pos        %.3f %.3f %.3f\n"
             "teleport-theta      %.3f\n"
             "teleport-verta      %.3f\n"
             "teleport-room       %d\n"
             "teleport-command    play.ps1 -Level <name> -TeleportMark <this file>\n"
             "replay-navigation   NOT a navigation source; the mark is the anchor\n",
             (double) v2w->m[3][0], (double) v2w->m[3][1], (double) v2w->m[3][2],
             (double) pl->vv_theta, (double) pl->vv_verta,
             (int) pl->registeredroom);
    }

    /* ---- [projection] ---------------------------------------------------- */
    if (g_ViBackData != NULL) {
        MCAT("\n[projection]\n"
             "source              g_ViBackData (fr.c VideoSettings). The matrix\n"
             "                    the interpreter actually received is printed\n"
             "                    in [renderer]; compare the two.\n"
             "near                %.3f\n"
             "far                 %.3f\n"
             "fovy                %.4f\n"
             "aspect              %.6f\n"
             "framebuffer         %dx%d\n"
             "view                %dx%d at %d,%d\n",
             (double) g_ViBackData->znear, (double) g_ViBackData->zfar,
             (double) g_ViBackData->fovy, (double) g_ViBackData->aspect,
             (int) g_ViBackData->bufx, (int) g_ViBackData->bufy,
             (int) g_ViBackData->viewx, (int) g_ViBackData->viewy,
             (int) g_ViBackData->viewleft, (int) g_ViBackData->viewtop);
    } else {
        MCAT("\n[projection]\nUNKNOWN - g_ViBackData is NULL.\n");
    }

    /* ---- [rooms] ---------------------------------------------------------
     *
     * FOUR DIFFERENT FACTS, AND THEY ARE NOT INTERCHANGEABLE. This section
     * used to lead with bgCopyVisibleRoomsToList under the heading
     * "rooms-wanted", and it read 0 on a frame with six rooms loaded and four
     * of them drawing at the centre of the screen. Presented that way, one
     * number silently stood for all four of:
     *
     *   1  what the portal/visibility traversal last decided
     *   2  what bg.c asked the RSP to draw this frame
     *   3  what the display-list interpreter was actually handed
     *   4  what the interpreter actually drew
     *
     * 1 and 2 are here. 3 and 4 are the renderer's to answer and the platform
     * layer appends them below, because only the renderer knows which lists
     * were branched to and what came out of them. */
    {
        s32 proom = sl_mark_player_room_safe(pl);
        MCAT("\n[rooms]\n"
             "player-room         %s%d   (bondviewGetCurrentPlayersRoom)\n"
             "registered-room     %d   (player->registeredroom)\n",
             proom < 0 ? "UNKNOWN, tile pointer is NULL: " : "", (int) proom,
             (int) pl->registeredroom);
    }

    /* 1. THE VISIBILITY PACKET - labelled for exactly what it is.
     *
     * bgCopyVisibleRoomsToList reads num_visible_rooms_in_cur_global_vis_packet
     * (bg.c:4569), which the portal traversal fills and then LEAVES at
     * whatever the last packet produced. A mark taken at a frame boundary
     * routinely finds it empty. It is reported because an empty packet next to
     * a full render list is itself information - but it is NOT the answer to
     * "which rooms did the game want", and it is no longer captioned as
     * though it were. */
    cnt = bgCopyVisibleRoomsToList(rooms, 64);
    MCAT("\nvisibility-snapshot %d room(s), read at the FRAME BOUNDARY from the\n"
         "                    current visibility packet (bgCopyVisibleRoomsToList,\n"
         "                    bg.c:4569). This packet is left over from the last\n"
         "                    portal traversal and is ROUTINELY EMPTY here - a 0\n"
         "                    is NOT evidence that no room was visible. Measured\n"
         "                    0 on a frame with six rooms loaded and four drawing\n"
         "                    at the centre of the screen.\n"
         "                    rooms:", (int) cnt);
    if (cnt <= 0)
        MCAT(" (none - see the caveat above)");
    for (i = 0; i < cnt; i++)
        MCAT(" %d", (int) rooms[i]);
    MCAT("\n");

    /* 2. THE RENDER REQUEST. dword_CODE_bss_8007FFA0[0 .. g_BgNumberOfRoomsDrawn)
     * is the array bg.c:671 iterates to call bgRenderRoomPrimary and bg.c:744
     * iterates again for bgRenderRoomSecondary - the SAME array for both
     * passes, so there is one requested set, not two. A room is in the
     * secondary pass only if it has a secondary list at all, which the room
     * table below shows. */
    {
        extern s32 g_BgNumberOfRoomsDrawn;
        extern s_bound_info dword_CODE_bss_8007FFA0[];
        s32 nd = g_BgNumberOfRoomsDrawn;
        s32 nsec = 0;
        MCAT("\nrequested-primary   %d room(s) passed to bgRenderRoomPrimary\n"
             "                    (bg.c:700, over dword_CODE_bss_8007FFA0)\n"
             "                    rooms:", (int) nd);
        if (nd <= 0) MCAT(" (none)");
        for (i = 0; i < nd && i < 64; i++)
            MCAT(" %d", (int) dword_CODE_bss_8007FFA0[i].roomid);
        MCAT("\n");

        /* THE PER-ROOM WINDOW. Each entry carries the screen rectangle the
         * portal traversal reached the room through, and bg.c:694 issues it
         * as the RDP scissor (bgScissorCurrentPlayerViewF) before the room's
         * list - on the cartridge nothing of the room is drawn outside it.
         * Printed in the same units the cartridge's table holds (framebuffer
         * pixels, top-left origin), so the two can be read side by side.
         * Measured 2026-09-17 at Dam mark 20260916-232914/mark-001: the
         * cartridge confines rooms 23/35/26-29/1/2 to [1,182]-[30,214] while
         * this build's window for the same rooms was the full view, which is
         * why room 1's authored sliver quad and fog wedge showed natively and
         * not on hardware. */
        MCAT("requested-windows   per entry: room, draw order, portal window\n"
             "                    [min.x,min.y]-[max.x,max.y] in framebuffer px\n");
        for (i = 0; i < nd && i < 64; i++)
            MCAT("                    room %-4d order %d  window [%.1f,%.1f]-[%.1f,%.1f]\n",
                 (int) dword_CODE_bss_8007FFA0[i].roomid,
                 (int) dword_CODE_bss_8007FFA0[i].unk1,
                 (double) dword_CODE_bss_8007FFA0[i].bbox.min.x,
                 (double) dword_CODE_bss_8007FFA0[i].bbox.min.y,
                 (double) dword_CODE_bss_8007FFA0[i].bbox.max.x,
                 (double) dword_CODE_bss_8007FFA0[i].bbox.max.y);

        MCAT("requested-secondary the SAME array is walked again at bg.c:762 for\n"
             "                    bgRenderRoomSecondary. Of the rooms above, the\n"
             "                    ones that own a secondary list are:");
        for (i = 0; i < nd && i < 64; i++) {
            s32 rid = dword_CODE_bss_8007FFA0[i].roomid;
            if (rid >= 0 && rid < g_MaxNumRooms
                && g_BgRoomInfo[rid].ptr_secondary_expanded_mapping_info != NULL) {
                MCAT(" %d", (int) rid);
                nsec++;
            }
        }
        if (nsec == 0) MCAT(" (none)");
        MCAT("\n");
    }

    MCAT("\nroom-table          loaded rooms and their display lists, so a dl=\n"
         "                    operand in [candidate-draws] can be resolved by eye\n"
         "                    as well as by the resolver. max=%d\n",
         (int) g_MaxNumRooms);
    for (i = 0; i < g_MaxNumRooms && i < 256; i++) {
        void *p = g_BgRoomInfo[i].ptr_expanded_mapping_info;
        void *q = g_BgRoomInfo[i].ptr_secondary_expanded_mapping_info;
        if (p == NULL && q == NULL) continue;
        if (g_BgRoomInfo[i].model_bin_loaded == 0) continue;
        MCAT("  room %-4d loaded=%d primary-dl=%08x secondary-dl=%08x\n",
             (int) i, (int) g_BgRoomInfo[i].model_bin_loaded,
             p != NULL ? (unsigned int) OS_K0_TO_PHYSICAL(p) : 0u,
             q != NULL ? (unsigned int) OS_K0_TO_PHYSICAL(q) : 0u);
    }
#undef MCAT
    return at;
}

#endif /* !__sgi */
