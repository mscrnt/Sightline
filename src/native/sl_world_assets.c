/**
 * sl_world_assets.c - the two questions the WORLD-ASSET provider (#68) has to
 * ask the game, and the only place in the tree that asks them.
 *
 *   1. WHICH ITEM is this stage?  -> a canonical item path, or nothing.
 *   2. WHERE IS THE CAMERA?       -> the world-to-view transform, in exactly
 *                                    the convention the renderer's own
 *                                    modelview uses.
 *
 * WHY IT IS HERE and not in src/gfx. src/native is compiled with the game
 * include path precisely so that native features can read a game struct
 * without src/game learning about them (see sl_game_query.c's header). The
 * renderer therefore reaches these through two extern declarations and never
 * includes a game header; src/game is not touched at all by this feature, so
 * there is no #ifndef __sgi island anywhere in it and the cartridge build is
 * unchanged by construction rather than by inspection.
 *
 * READ-ONLY. Nothing here writes a game field, allocates, or has an effect of
 * any kind on the simulation. Both functions are pure reads of state the frame
 * has already computed, and both answer "no" safely when there is no level, no
 * player, or no camera yet - which is what makes a world asset that fails to
 * resolve cost the accepted picture and nothing else.
 */
#include <ultra64.h>
#include <bondgame.h>
#include <boss.h>
#include "bondview.h"
#include "player.h"
#include "bg.h"
#include "frametiming.h"
#include "lightfixture.h"
#include "bgfog.h"
#include "image_bank.h"
#include <stdlib.h>
#include "unk_092E50.h"

extern s32 g_StageNum;
extern struct levelentry levelinfotable[];
extern s32 levelentry_index;

/**
 * THE GAME'S OWN FRAME CLOCK, in VI frames, for material animation (#69).
 *
 * `currentFrameCounter` (frametiming.c) is what the game adds every retrace
 * delta to on every game frame - paused or not, since the pause only zeroes
 * `g_ClockTimer`, the simulation's share of those retraces (lv.c:1019-1027).
 * That is the clock the remaster's animated water runs on: measured on the
 * console, its time constant advances by exactly one frame period per
 * rendered frame and keeps advancing behind the pause screen. Here one frame
 * is one VI retrace, 1/60 s, and the pump emits exactly one retrace per
 * game frame (sl_main.c prints this counter beside the retrace count and
 * requires them to agree), so the clock is deterministic under a replay,
 * independent of how many frames are presented, and never the wall clock.
 *
 * Read-only, like everything in this file; the simulation never sees who
 * read it. Returns 1 and the count, 0 before the counter exists.
 */
int sl_wa_frame_clock(unsigned long *frames);
int sl_wa_frame_clock(unsigned long *frames)
{
    if (frames == (unsigned long *) 0) return 0;
    if (currentFrameCounter < 0) return 0;
    *frames = (unsigned long) currentFrameCounter;
    return 1;
}

/**
 * The stage now running, as the game numbers it (LEVELID). The texture
 * provider names a set's per-stage table of carried coordinates by it (#77,
 * src/gfx/sl_gfx_texprov.c). Read-only.
 */
int sl_wa_stage_num(void);
int sl_wa_stage_num(void)
{
    return (int) g_StageNum;
}

/**
 * SL_WA_PREVIEW=0: the switch back to the texture set's look on a stage whose
 * replacement background is the default (Facility, #77 - it was a preview
 * behind SL_WA_PREVIEW=1 until accepted - and Runway, #78). With it such a
 * stage answers NULL below and draws exactly as it does with no world asset
 * at all: the game's rooms with the set's artwork. Unset, or any other value (the earlier
 * SL_WA_PREVIEW=1 included), draws the replacement. One cached getenv.
 */
static int wa_pack_look(void)
{
    static int on = -1;
    if (on < 0) {
        const char *v = getenv("SL_WA_PREVIEW");
        on = (v != (const char *) 0 && v[0] == '0' && v[1] == '\0');
    }
    return on;
}

/**
 * The canonical ITEM PATH for the stage now running, or NULL.
 *
 * This is the whole of the provider's stage vocabulary and it is deliberately
 * a lookup rather than a derivation: an item path belongs to the asset source's
 * namespace, not to the game's enum, and the two do not always spell a level
 * the same way. Deriving one from the other would be a guess that happens to
 * work for the first entry.
 *
 * A stage with no entry returns NULL, which is not a special case but the
 * mechanism: no path means no lookup, no file access and no draw, so such a
 * stage is a negative control by construction rather than by a flag someone
 * has to remember to clear. Adding the next item is adding a line here and
 * preparing a file - no renderer change, and no per-level data in the tree.
 *
 * Every entry but the Dam's yields to SL_WA_PREVIEW=0, which keeps the
 * game's own background there with the texture set's artwork on it
 * (wa_pack_look). The Dam's has no such switch. Bunker 1's item is the
 * source's `bunker`: the source spells the stage without the game's number.
 * Bunker 2's is `bunker2`, an item of its own - the game builds that stage
 * from another background file (bg_sevb_all_p, where Bunker 1 loads
 * bg_sev_all_p; levelinfotable in bg.c, the same scale column 23.148148),
 * and the source carries `background/bunker2` and `skydome/bunker2` beside
 * Bunker 1's. Silo's is `silo`, Frigate's `frigate`. Statue's is
 * `statuepark`, the source's own spelling of the stage: its listing carries
 * `background/statuepark` and nothing named `statue`, and no skydome for the
 * stage, so sl_wa_stage_sky_item_path names an item no root holds and the
 * game's own sky stays. The item reads at levelinfotable's column like every
 * other (0.0801, so 1248.44 item units per room unit; fitted 1248.42 by least
 * squares over the vertices the two backgrounds share). Archives' is
 * `archives`, the stage's own name in the source (`background/archives`,
 * no skydome: an interior), read at the column 54.347824, so 1.84 item units
 * per room unit - the factor sl_wa_source_scale's fit already measured.
 * Streets' is `streets` (`background/streets`; the source holds no dome for
 * it, and the presentation lends it one - D-044, sl_presentation_sky_lent),
 * read at the column 42.372883, so 2.36 item units per room unit.
 * Depot's is `depot` (`background/depot`, no dome in the source), read at
 * the column 17.605633, so 5.68 item units per room unit.
 * Train's is `train` (`background/train`, no dome in the source), read at
 * the column 19.53125, so 5.12 item units per room unit (fitted 5.120057 by
 * least squares over the 4618 room corners that land on an item vertex).
 * Jungle's is `jungle` (`background/jungle`, no dome in the source), read
 * at the column 6.6844921, so 14.96 item units per room unit (fitted
 * 14.960104 over the 1219 room corners that land on an item vertex; the
 * remaster's terrain is remodelled, so most of the game's corners do not).
 * Control's is `control` (`background/control`, no dome in the source: an
 * interior), read at the column 80.645164, so 1.24 item units per room unit
 * (fitted 1.239988 over the 3355 room corners that land on an item vertex).
 * Caverns' is `cavern`, the source's MODEL spelling (`background/cavern`;
 * `background/caves` is the multiplayer Caves, bg_oat_all_p), read at the
 * column 13.44086, so 7.44 item units per room unit (fitted 7.439977 over the
 * 2982 room corners that land on an item vertex; the extents agree to 7.440
 * on all three axes).
 * Cradle's is `cradle` (`background/cradle`, no dome in the source: one is
 * lent, sl_presentation_sky_lent), read at the column 43.103451, so 2.32 item
 * units per room unit (fitted 2.319943 over the 1107 room corners that land
 * on an item vertex; the item reaches past the rooms with the canyon
 * around them).
 * Aztec's is `aztec` (`background/aztec`; `temple` and `multitemple` are
 * other stages - 4 and 1 of the 7286 room corners land on their vertices),
 * read at the column 52.083332, so 1.92 item units per room unit (fitted
 * 1.920040 over the 6123 room corners that land on an item vertex; 7275 on
 * the source's original generation, of which the remaster remodels part).
 * Egyptian's is `temple` (`background/temple`: its extents are the game's
 * rooms' times 4.24 on all three axes, and 4491 of bg_cryp_all_p's 4851
 * corners lie within one room unit of a vertex of its original generation,
 * 3925 of the new, with no mean offset - the game's integer rounding of the
 * source's positions; `multitemple` and `aztec` hold 0 and 2), read at the
 * column 23.584906, so 4.24 item units per room unit (fitted 4.239944). The
 * launcher's `temple` is another stage - the multiplayer Temple, stage 38 -
 * which is why the item is named here rather than resolved by its name.
 *
 * Surface 2 answers `surface`. The game builds both Surface stages from one
 * background file (bg_sevx_all_p, levelinfotable in bg.c, with the same
 * scale column), and the source carries one background item for the pair -
 * `background/surface` and no `background/surface2`. Its sky is the stage's
 * own: see sl_wa_stage_sky_item_path.
 */
const char *sl_wa_stage_item_path(void);
const char *sl_wa_stage_item_path(void)
{
    switch (g_StageNum) {
    case LEVELID_DAM:      return "dam";
    case LEVELID_FACILITY: return wa_pack_look() ? (const char *) 0 : "facility";
    case LEVELID_RUNWAY:   return wa_pack_look() ? (const char *) 0 : "runway";
    case LEVELID_SURFACE:  return wa_pack_look() ? (const char *) 0 : "surface";
    case LEVELID_SURFACE2: return wa_pack_look() ? (const char *) 0 : "surface";
    case LEVELID_BUNKER1:  return wa_pack_look() ? (const char *) 0 : "bunker";
    case LEVELID_BUNKER2:  return wa_pack_look() ? (const char *) 0 : "bunker2";
    case LEVELID_SILO:    return wa_pack_look() ? (const char *) 0 : "silo";
    case LEVELID_FRIGATE:  return wa_pack_look() ? (const char *) 0 : "frigate";
    case LEVELID_STATUE:   return wa_pack_look() ? (const char *) 0 : "statuepark";
    case LEVELID_ARCHIVES: return wa_pack_look() ? (const char *) 0 : "archives";
    case LEVELID_STREETS:  return wa_pack_look() ? (const char *) 0 : "streets";
    case LEVELID_DEPOT:    return wa_pack_look() ? (const char *) 0 : "depot";
    case LEVELID_TRAIN:    return wa_pack_look() ? (const char *) 0 : "train";
    case LEVELID_JUNGLE:   return wa_pack_look() ? (const char *) 0 : "jungle";
    case LEVELID_CONTROL:  return wa_pack_look() ? (const char *) 0 : "control";
    case LEVELID_CAVERNS:  return wa_pack_look() ? (const char *) 0 : "cavern";
    case LEVELID_CRADLE:   return wa_pack_look() ? (const char *) 0 : "cradle";
    case LEVELID_AZTEC:    return wa_pack_look() ? (const char *) 0 : "aztec";
    case LEVELID_EGYPT:    return wa_pack_look() ? (const char *) 0 : "temple";
    default:              return (const char *) 0;
    }
}

/**
 * The SKYDOME item for the stage now running, or NULL: the background item
 * (sl_wa_stage_item_path) on every stage but two. Surface 2 shares Surface's
 * background item, but the source carries a dome of its own for it,
 * `skydome/surface2`, beside `skydome/surface`. Egyptian's background is the
 * source's `temple`, but its domes are named as the stages are (`caverns`,
 * `complex`, `surface2`): `skydome/temple` names the multiplayer Temple and
 * `skydome/egyptian` this stage. The eleven domes read identically - one
 * geometry, the same two images - so the name decides no pixel. A NULL
 * background item is a NULL sky item, so SL_WA_PREVIEW=0 keeps the game's
 * sky as well.
 */
const char *sl_wa_stage_sky_item_path(void);
const char *sl_wa_stage_sky_item_path(void)
{
    const char *item = sl_wa_stage_item_path();
    if (item == (const char *) 0) return (const char *) 0;
    if (g_StageNum == LEVELID_SURFACE2) return "surface2";
    if (g_StageNum == LEVELID_EGYPT) return "egyptian";
    return item;
}

/**
 * THE SOURCE'S OWN SCALE: the factor that takes a stage background item's
 * coordinates into the bg data's room coordinates, or 0 with no stage.
 *
 * The source authors every stage background at the room coordinates TIMES
 * 100 / the stage's last levelinfotable column (`unknownfloat`, bg.c) - so
 * this returns that column / 100. Measured by least squares over the
 * coinciding vertices of the two backgrounds, the remaster's and the ROM's
 * rooms: Facility 1.560002 against 100 / 64.102562 = 1.560000, Bunker 4.319999
 * (4.32), Silo 3.440084 (3.440091), Archives 1.840005 (1.84), Frigate
 * 2.719945 (2.72), Cradle 2.319957 (2.32). The Dam's column is 100, so its
 * factor is exactly 1.0 - which is why its item reads as authored in the room
 * coordinates themselves.
 *
 * Read-only, like the others.
 */
int sl_wa_source_scale(float *s);
int sl_wa_source_scale(float *s)
{
    f32 v;

    if (s == (float *) 0) return 0;
    if (ptr_bgdata_room_fileposition_list == (bg_room_data *) 0) return 0;
    v = levelinfotable[levelentry_index].unknownfloat / 100.0f;
    if (!(v > 0.0f)) return 0;
    *s = (float) v;
    return 1;
}

/**
 * The WORLD-TO-VIEW transform, row-vector order (v' = v * M), row 3 the
 * translation - the same convention src/gfx/sl_gfx_dl.c stores its modelview
 * stack in, so a world position put through this lands exactly where the
 * game's own world geometry lands.
 *
 * DERIVED, NOT GUESSED. The player carries `viewtoworldmtxf`, the VIEW-TO-WORLD
 * matrix whose row 3 is the eye position (the same field the F8 mark cites for
 * the eye, src/native/sl_game_query.c). Its inverse is what a world position
 * needs, and because the matrix is rigid - a rotation and a translation - the
 * inverse is the transpose of the rotation with the translation carried
 * through it, which is computed here exactly rather than by a general inverse:
 *
 *      v_view = (v_world - t) * R^T
 *
 * Nothing about the camera is reconstructed from angles, and no assumption is
 * made about which way the axes point: whatever the game's camera is doing,
 * including a cutscene or a death spin, this follows it.
 *
 * Returns 1 and fills m[16] when there is a camera, 0 otherwise - during the
 * front end, a level load, or any frame before the first view matrix exists.
 */
/**
 * The RENDER ORIGIN the frame's world geometry is drawn relative to, or 0.
 *
 * GoldenEye does NOT submit world geometry in absolute world coordinates. Every
 * room's transform is built as (the room's own scaled origin) MINUS the player's
 * `current_model_pos` (bgroomtrans.c:216-218), and every view matrix handed to
 * the RSP has the same point subtracted from its translation row
 * (bondview2.c:11415-11417, sub_GAME_7F08BEEC). So the space the display list
 * actually works in is world MINUS this point - a floating origin that keeps
 * the s15.16 matrices precise near the player, and that MOVES whenever the
 * player's room changes.
 *
 * A world asset carries ABSOLUTE world coordinates, so it has to be told what
 * the frame's origin is before it can be put through the frame's own matrices.
 * Reading it is what makes the asset's anchoring explicit rather than inherited
 * from whichever matrix happened to be in force.
 *
 * Read-only, like the two above: `current_model_pos` is state the frame has
 * already computed and nothing here writes it.
 */
int sl_wa_render_origin(float *o);
int sl_wa_render_origin(float *o)
{
    const struct player *pl = g_CurrentPlayer;

    if (o == (float *) 0) return 0;
    if (pl == (const struct player *) 0) return 0;
    o[0] = pl->current_model_pos.f[0];
    o[1] = pl->current_model_pos.f[1];
    o[2] = pl->current_model_pos.f[2];
    return 1;
}

/**
 * THE STAGE'S LEVEL SCALE, `room_data_float2`, or 0 when there is no stage.
 *
 * This is the number `setupRoomTransformationMatrix` puts on the diagonal of
 * every room's matrix and multiplies every room position by
 * (bgroomtrans.c:211-218), so it is exactly the factor between the bg data's
 * own room coordinates and the space the frame's geometry is laid out in. A
 * world-anchored asset authored in the bg data's coordinates needs it, and
 * needs it READ rather than assumed: it is per level (4.2801 at the Dam), not
 * a constant.
 */
int sl_wa_level_scale(float *s);
int sl_wa_level_scale(float *s)
{
    if (s == (float *) 0) return 0;
    if (ptr_bgdata_room_fileposition_list == (bg_room_data *) 0) return 0;
    if (!(room_data_float2 > 0.0f)) return 0;
    *s = (float) room_data_float2;
    return 1;
}

/**
 * THE SEA THE GAME IS DRAWING THIS FRAME (D-039), or 0 when the environment
 * draws none (`IsWater` clear).
 *
 * sky.c lays its sea on the plane y = `WaterRepeat` (skyIsCornerInWater) and
 * gives each corner the texture coordinates world x and world z plus
 * `g_SkyCloudOffset` (sky.c:435-442, scaled by 65535 / 65536 in
 * sub_GAME_7F097388), seen from bondviewGetCurrentPlayersPosition. Those are
 * read here so the renderer can run the coordinates backwards into the
 * point of the plane each pixel shows. Also the environment's sky colour and
 * cloud colour (the sky's horizon and its cloud layer, bgfog.h) and the
 * game's clock in seconds (`g_GlobalTimer`, 60 ticks a second) for the
 * motion. Reads only.
 */
extern s32 g_GlobalTimer;
int sl_wa_sea_frame(float *eye, float *sea_y, float *z_offset, float *seconds,
                    float *sky_rgb, float *cloud_rgb);
int sl_wa_sea_frame(float *eye, float *sea_y, float *z_offset, float *seconds,
                    float *sky_rgb, float *cloud_rgb)
{
    struct CurrentEnvironmentRecord *e;
    coord3d *p;
    if (eye == (float *) 0 || sea_y == (float *) 0 || z_offset == (float *) 0
        || seconds == (float *) 0 || sky_rgb == (float *) 0 || cloud_rgb == (float *) 0)
        return 0;
    e = fogGetCurrentEnvironmentp();
    if (e == (struct CurrentEnvironmentRecord *) 0 || !e->IsWater) return 0;
    if (g_CurrentPlayer == (struct player *) 0) return 0;
    p = bondviewGetCurrentPlayersPosition();
    if (p == (coord3d *) 0) return 0;
    eye[0] = p->f[0]; eye[1] = p->f[1]; eye[2] = p->f[2];
    *sea_y = e->WaterRepeat;
    *z_offset = g_SkyCloudOffset;
    *seconds = (float) g_GlobalTimer / 60.0f;
    sky_rgb[0] = (float) e->Red;  sky_rgb[1] = (float) e->Green;  sky_rgb[2] = (float) e->Blue;
    cloud_rgb[0] = e->CloudRed;   cloud_rgb[1] = e->CloudGreen;   cloud_rgb[2] = e->CloudBlue;
    return 1;
}

/**
 * THE STAGE'S VISIBILITY SCALE, `mCurrentLevelVisibilityScale`, or 0.
 *
 * The factor the GAME puts on every matrix it hands the RSP, and the one thing
 * a world-anchored asset cannot infer from geometry. `bg.c:1088-1091` reads it
 * out of the level table and hands it to `matrix_4x4_7F058C4C`, which stores
 * `65535 * scale` as the fixed-point conversion's own multiplier
 * (`matrixmath.c:478`); `matrix_4x4_f32_to_s32` then applies it to the first
 * three columns of every matrix converted (`matrixmath.c:495-511`), and
 * `bondview2.c:8798-8800` applies it a second time, explicitly and by name, to
 * the player's own world matrix. So the space the frame's world matrices
 * consume is the room space TIMES this number, and the asset has to be put
 * into that space rather than into the room space.
 *
 * It is a per-LEVEL value and it is 1.0 almost everywhere: of the twenty-odd
 * entries in `levelinfotable` only the Dam and Surface carry 0.2 (`bg.c:291`,
 * `bg.c:294`). That is why nothing outside those two stages can show this, and
 * why reading it - rather than assuming the 1.0 that holds everywhere else - is
 * the whole of the contract.
 *
 * Read-only, like the others.
 */
int sl_wa_visibility_scale(float *s);
int sl_wa_visibility_scale(float *s)
{
    f32 v;

    if (s == (float *) 0) return 0;
    if (ptr_bgdata_room_fileposition_list == (bg_room_data *) 0) return 0;
    v = bgGetLevelVisibilityScale();
    if (!(v > 0.0f)) return 0;
    *s = (float) v;
    return 1;
}

/**
 * THE STAGE'S OWN EXTENT, in the space its room transforms are built in.
 *
 * Diagnostic, and it exists because a world-anchored REPLACEMENT has to be put
 * where the original is, and the only honest way to check that is to compare
 * the two extents in ONE space. `setupRoomTransformationMatrix` builds every
 * room's matrix as (the room's own position TIMES the level scale) minus the
 * player's `current_model_pos` (bgroomtrans.c:211-218), so the room positions
 * times that scale are exactly the coordinates the frame's geometry is laid
 * out in before the floating origin is removed.
 *
 * Read-only, like the three above, and it walks the room list the same way
 * bg.c's own loader terminates it. Returns the number of rooms, 0 if there is
 * no stage.
 */
int sl_wa_level_box(float *lo, float *hi, float *scale);
int sl_wa_level_box(float *lo, float *hi, float *scale)
{
    const bg_room_data *rooms = ptr_bgdata_room_fileposition_list;
    int i, j, n = 0;
    f32 s = room_data_float2;

    if (lo == (float *) 0 || hi == (float *) 0) return 0;
    if (rooms == (const bg_room_data *) 0) return 0;
    for (j = 0; j < 3; j++) { lo[j] = 1e30f; hi[j] = -1e30f; }
    for (i = 1; i < 1024 && rooms[i].pPriMappingBin != NULL; i++) {
        for (j = 0; j < 3; j++) {
            float v = rooms[i].pos.f[j] * s;
            if (v < lo[j]) lo[j] = v;
            if (v > hi[j]) hi[j] = v;
        }
        n++;
    }
    if (scale != (float *) 0) *scale = (float) s;
    return n;
}

int sl_wa_view_matrix(float *m);
int sl_wa_view_matrix(float *m)
{
    const struct player *pl = g_CurrentPlayer;
    const Mtxf *v2w;
    int r, c;

    if (m == (float *) 0) return 0;
    if (pl == (const struct player *) 0) return 0;
    v2w = pl->viewtoworldmtxf;
    if (v2w == (const Mtxf *) 0) return 0;

    for (r = 0; r < 3; r++)
        for (c = 0; c < 3; c++)
            m[r * 4 + c] = v2w->m[c][r];          /* R^T */
    m[3] = m[7] = m[11] = 0.0f;
    for (c = 0; c < 3; c++)
        m[12 + c] = -(v2w->m[3][0] * v2w->m[c][0]
                    + v2w->m[3][1] * v2w->m[c][1]
                    + v2w->m[3][2] * v2w->m[c][2]);   /* -t * R^T */
    m[15] = 1.0f;
    return 1;
}

/**
 * THE GAME'S SHOT-OUT LIGHTS, one slot of lightfixture.c's persistent record
 * at a time, for a replacement background that has to show the same state.
 *
 * GoldenEye keeps what the player has shot out in `darkened_light_table`
 * (lightfixture.c): (room, vertex) pairs whose vertex colours it shifts right
 * by two in place, re-applied whenever the room's vertices are streamed back
 * in. That table is the whole of the state - which light, and that it is out -
 * and the renderer reads it here rather than deciding anything itself: no hit
 * test, no damage rule, no distance, no timer.
 *
 * For slot `i` (0 .. sl_wa_light_slots() - 1): 0 when the slot is empty; 1
 * with `*key` = room << 16 | vertex when the slot is in use but the room's
 * vertices are not resident, so no position can be read this frame; 2 with
 * `*key` and `p[3]` = the darkened vertex in the coordinates a stage
 * background item is authored in (the room's origin plus the vertex's offset,
 * through sl_wa_source_scale - the room data's own coordinates on the Dam)
 * when they are.
 *
 * Read-only, like everything in this file.
 */
extern struct s_darkened_light darkened_light_table[];

unsigned sl_wa_light_slots(void);
unsigned sl_wa_light_slots(void)
{
    return 0x200u;     /* DARKENED_LIGHT_TABLE_MAX, lightfixture.c */
}

int sl_wa_light_darkened(unsigned i, unsigned *key, float *p);
int sl_wa_light_darkened(unsigned i, unsigned *key, float *p)
{
    const bg_room_data *rooms = ptr_bgdata_room_fileposition_list;
    unsigned room, vtx;
    const Vtx *v;
    float src;

    if (i >= sl_wa_light_slots() || key == (unsigned *) 0 || p == (float *) 0) return 0;
    room = darkened_light_table[i].room_index;
    vtx = darkened_light_table[i].vtx_index;
    if (room == 0u || rooms == (const bg_room_data *) 0) return 0;
    *key = (room << 16) | vtx;
    v = (const Vtx *) g_BgRoomInfo[room].vertices;
    if (v == (const Vtx *) 0) return 1;
    if (!sl_wa_source_scale(&src)) return 1;
    p[0] = (rooms[room].pos.f[0] + (float) v[vtx].v.ob[0]) / src;
    p[1] = (rooms[room].pos.f[1] + (float) v[vtx].v.ob[1]) / src;
    p[2] = (rooms[room].pos.f[2] + (float) v[vtx].v.ob[2]) / src;
    return 2;
}

/**
 * HOW FAR ONE FIXTURE REACHES, in the same item coordinates: the distance
 * lightIsCoordNearDarkenedVertex (lightfixture.c) uses to decide that a
 * triangle belongs to a fixture that has been shot - a sum of the three
 * absolute axis differences below `room_data_float1 * 100`. 0 with no stage.
 */
float sl_wa_light_reach(void);
float sl_wa_light_reach(void)
{
    float src;

    if (ptr_bgdata_room_fileposition_list == (bg_room_data *) 0) return 0.0f;
    if (!sl_wa_source_scale(&src)) return 0.0f;
    return (float) (get_room_data_float1() * 100.0f) / src;
}

/**
 * THE GAME'S CLOUD LAYER, as sky.c draws it (D-023): the cloud phase skyTick
 * advances (g_SkyCloudOffset, in the texture's 10.5 fixed-point units, one
 * g_ClockTimer step per game tick - none while lv.c holds the timer at zero
 * for locked controls or the multiplayer pause; the solo watch does not stop
 * it - wrapped at 4096), the height of the cloud plane (the environment's
 * CloudRepeat, sky.c's "skyheight"), the eye the plane is cast from
 * (bondviewGetCurrentPlayersPosition, sky.c's own) and the size of the cloud
 * image the environment names (SkyImageId), which fixes its repeat.
 *
 * Returns 1 when the environment draws a cloud layer and all of it can be
 * read, 0 otherwise (no environment, no clouds, no player, no image table).
 * Read-only, like everything in this file: the simulation never sees who
 * read it.
 */
int sl_wa_sky_cloud(float *phase, float *height, float *eye, unsigned *img_w,
                    unsigned *img_h);
int sl_wa_sky_cloud(float *phase, float *height, float *eye, unsigned *img_w,
                    unsigned *img_h)
{
    struct CurrentEnvironmentRecord *env = fogGetCurrentEnvironmentp();
    coord3d *p;
    const struct sImageTableEntry *im;

    if (phase == (float *) 0 || height == (float *) 0 || eye == (float *) 0
        || img_w == (unsigned *) 0 || img_h == (unsigned *) 0) return 0;
    if (env == (struct CurrentEnvironmentRecord *) 0 || !env->Clouds) return 0;
    if (g_CurrentPlayer == (struct player *) 0 || skywaterimages == (struct sImageTableEntry *) 0)
        return 0;
    p = bondviewGetCurrentPlayersPosition();
    if (p == (coord3d *) 0) return 0;
    im = &skywaterimages[env->SkyImageId];
    if (im->width == 0u || im->height == 0u) return 0;
    *phase = g_SkyCloudOffset;
    *height = env->CloudRepeat;
    eye[0] = p->f[0];
    eye[1] = p->f[1];
    eye[2] = p->f[2];
    *img_w = im->width;
    *img_h = im->height;
    return 1;
}

/**
 * D-048 THE GAME'S WATER, as the water controller leaves it: the three phases
 * sub_GAME_7F092E50 (unk_092E50.c) advances once per game tick from lv.c -
 * the S and T origins of the water setup's tile 0 in quarter-texels (0.25 and
 * 0.1 a g_ClockTimer step, wrapped at 256) and the blend phase in radians
 * (0.04 a step, wrapped at the controller's 6.2831802) - the values its own
 * setup lists are rewritten from, and the ones B-109 hands the native renderer
 * for the game's own water. Always 1 (the three are plain globals, zeroed at
 * boot by initunk_007290.c). Read-only, like everything in this file.
 */
int sl_wa_water_phase(float *ps, float *pt, float *pw);
int sl_wa_water_phase(float *ps, float *pt, float *pw)
{
    if (ps == (float *) 0 || pt == (float *) 0 || pw == (float *) 0) return 0;
    *ps = flt_CODE_bss_80079E80;
    *pt = flt_CODE_bss_80079E84;
    *pw = flt_CODE_bss_80079E88;
    return 1;
}

/**
 * D-030 THE GAME'S ATMOSPHERE, as sky.c consumes it: the environment's fog
 * colour (Red/Green/Blue - the colour skyRender fills with, the colour every
 * fogged draw fades to, and the ENV colour of the cloud layer's combiner) and
 * its cloud colour (CloudRed/Green/Blue, which skyChooseCloudVtxColour lays
 * over the fog colour overhead), both 0..255. Returns 1 when an environment
 * is in force, 0 otherwise. Read-only, like everything in this file.
 */
int sl_wa_game_atmosphere(float fog[3], float cloud[3]);
int sl_wa_game_atmosphere(float fog[3], float cloud[3])
{
    struct CurrentEnvironmentRecord *env = fogGetCurrentEnvironmentp();

    if (fog == (float *) 0 || cloud == (float *) 0) return 0;
    if (env == (struct CurrentEnvironmentRecord *) 0) return 0;
    fog[0] = (float) env->Red;
    fog[1] = (float) env->Green;
    fog[2] = (float) env->Blue;
    cloud[0] = env->CloudRed;
    cloud[1] = env->CloudGreen;
    cloud[2] = env->CloudBlue;
    return 1;
}

/**
 * D-031 A BORROWED SKY: the same two answers for an environment row the game
 * is NOT in - read from the game's own table (fog_tables, bgfog.c), field for
 * field as fogLoadCurrentEnvironment copies a row into the environment in
 * force - so a stage with no sky of its own (Bunker 1) can draw another
 * stage's sky exactly as that stage draws it (row 36: Surface 1). The phase
 * and the eye are the live ones, as sl_wa_sky_cloud's: skyTick advances the
 * phase on every stage. 0 when the table has no such row, the row has no
 * cloud layer, or (for the cloud) there is no player or image table.
 * Read-only, like everything in this file: no table entry and no environment
 * field is written.
 */
extern EnvironmentRecord fog_tables[];

static const EnvironmentRecord *wa_env_row(unsigned env_id)
{
    const EnvironmentRecord *r;
    for (r = &fog_tables[0]; r->Id != 0; r++)
        if ((unsigned) r->Id == env_id) return r;
    return (const EnvironmentRecord *) 0;
}

int sl_wa_game_atmosphere_row(unsigned env_id, float fog[3], float cloud[3]);
int sl_wa_game_atmosphere_row(unsigned env_id, float fog[3], float cloud[3])
{
    const EnvironmentRecord *r = wa_env_row(env_id);

    if (fog == (float *) 0 || cloud == (float *) 0) return 0;
    if (r == (const EnvironmentRecord *) 0) return 0;
    fog[0] = (float) r->Sky.Red;
    fog[1] = (float) r->Sky.Green;
    fog[2] = (float) r->Sky.Blue;
    cloud[0] = (float) r->Sky.CloudRed;
    cloud[1] = (float) r->Sky.CloudGreen;
    cloud[2] = (float) r->Sky.CloudBlue;
    return 1;
}

int sl_wa_sky_cloud_row(unsigned env_id, float *phase, float *height, float *eye,
                        unsigned *img_w, unsigned *img_h);
int sl_wa_sky_cloud_row(unsigned env_id, float *phase, float *height, float *eye,
                        unsigned *img_w, unsigned *img_h)
{
    const EnvironmentRecord *r = wa_env_row(env_id);
    coord3d *p;
    const struct sImageTableEntry *im;

    if (phase == (float *) 0 || height == (float *) 0 || eye == (float *) 0
        || img_w == (unsigned *) 0 || img_h == (unsigned *) 0) return 0;
    if (r == (const EnvironmentRecord *) 0 || !r->Sky.Clouds) return 0;
    if (g_CurrentPlayer == (struct player *) 0 || skywaterimages == (struct sImageTableEntry *) 0)
        return 0;
    p = bondviewGetCurrentPlayersPosition();
    if (p == (coord3d *) 0) return 0;
    im = &skywaterimages[(s16) r->Sky.SkyImageId];
    if (im->width == 0u || im->height == 0u) return 0;
    *phase = g_SkyCloudOffset;
    *height = (float) r->Sky.CloudRepeat;
    eye[0] = p->f[0];
    eye[1] = p->f[1];
    eye[2] = p->f[2];
    *img_w = im->width;
    *img_h = im->height;
    return 1;
}

/**
 * #90 THE GAME'S CLOUD LAYER, WHERE IT LIES ON THE SCREEN: the rows skyRender
 * (sky.c) shades its cloud polygon between, and the cloud fraction it gives
 * each, for the left (side 0) and right (side 1) edges of the view. Per side:
 *
 *   frac_a at row_a and frac_b at row_b, linear in the row between them and
 *   continued past them (off the picture, where a triangle reaching into it
 *   needs its far corner), held to 0..1 - but where an anchor is the HORIZON
 *   EDGE the game lays down (skyCalculateEdgeVertex; its fraction is 1, the
 *   fog colour), the far side of it is no cloud at all: the colour
 *   skyRender filled the view with (`fill_below` / `fill_above`).
 *
 * The corners are the game's own: skyGetWorldPosFromScreenPos (which adds the
 * environment's WaterConcavity to every row - Streets' 25 - so the game's
 * horizon edge stands that much ABOVE the true horizon) and
 * skyIsScreenCornerInSky (frac = 1 - min(1, 2 y / |xz|)), and the edge row is
 * skyRender's own: the top row plus the height times the top corner's y over
 * the difference of the two corners' y. Rows are the game's screen rows
 * (`top`, `height`: getPlayer_c_screentop/height; the view's top row maps to
 * the top of the projected picture).
 *
 * Measured on Streets (row 29) at the approach to the end of the street
 * (eye y 97, pitch 2.9): the edge row 90.6 of 10..230, the top corner's
 * fraction 0.389; the game's picture holds its fill colour from that row
 * down to the scenery.
 *
 * Layout: rows[0] the top row, rows[1] the height, then per side s the four
 * floats rows[2 + 4s ..] = row_a, frac_a, row_b, frac_b; fill[2s] is
 * `fill_above`, fill[2s + 1] `fill_below`.
 *
 * Returns 1 with the anchors written, 0 with no environment, no cloud layer
 * or no player. Read-only: the game's functions it calls read the player's
 * view, the eye and the environment and write only their outputs.
 */
extern void skyGetWorldPosFromScreenPos(f32 offset_x, f32 offset_y, coord3d *out);
extern bool skyIsScreenCornerInSky(coord3d *corner3dpos, coord3d *dstpos, f32 *dstfrac);
int sl_wa_sky_screen(float rows[10], int fill[4]);
int sl_wa_sky_screen(float rows[10], int fill[4])
{
    struct CurrentEnvironmentRecord *env = fogGetCurrentEnvironmentp();
    f32 w, h, top;
    int side;

    if (rows == (float *) 0 || fill == (int *) 0) return 0;
    if (env == (struct CurrentEnvironmentRecord *) 0 || !env->Clouds) return 0;
    if (g_CurrentPlayer == (struct player *) 0) return 0;
    w = getPlayer_c_screenwidth();
    h = getPlayer_c_screenheight();
    top = getPlayer_c_screentop();
    if (!(h > 0.0f) || !(w > 0.0f)) return 0;
    rows[0] = top;
    rows[1] = h;
    for (side = 0; side < 2; side++) {
        coord3d up, dn, pa, pb;
        f32 fa = 1.0f, fb = 1.0f;
        f32 x = side ? w - 0.1f : 0.0f;
        int in_up, in_dn;

        skyGetWorldPosFromScreenPos(x, 0.0f, &up);
        skyGetWorldPosFromScreenPos(x, h - 0.1f, &dn);
        in_up = skyIsScreenCornerInSky(&up, &pa, &fa) ? 1 : 0;
        in_dn = skyIsScreenCornerInSky(&dn, &pb, &fb) ? 1 : 0;
        fill[2 * side] = 0;
        fill[2 * side + 1] = 0;
        if (in_up && in_dn) {                       /* the whole edge in the sky */
            rows[2 + 4 * side] = top;       rows[3 + 4 * side] = fa;
            rows[4 + 4 * side] = top + h;   rows[5 + 4 * side] = fb;
        } else if (in_up != in_dn && up.f[1] != dn.f[1]) {
            f32 edge = top + h * (up.f[1] / (up.f[1] - dn.f[1]));
            if (in_up) {                            /* the sky above the edge */
                rows[2 + 4 * side] = top;   rows[3 + 4 * side] = fa;
                rows[4 + 4 * side] = edge;  rows[5 + 4 * side] = 1.0f;
                fill[2 * side + 1] = 1;
            } else {                                /* ... below it */
                rows[2 + 4 * side] = edge;  rows[3 + 4 * side] = 1.0f;
                rows[4 + 4 * side] = top + h; rows[5 + 4 * side] = fb;
                fill[2 * side] = 1;
            }
        } else {                                    /* no sky on this edge */
            rows[2 + 4 * side] = top;       rows[3 + 4 * side] = 1.0f;
            rows[4 + 4 * side] = top + h;   rows[5 + 4 * side] = 1.0f;
            fill[2 * side] = 1;
            fill[2 * side + 1] = 1;
        }
    }
    return 1;
}

/**
 * WHETHER THE STAGE'S BACKDROP IS THE GAME'S FLAT FILL: 1 when the
 * environment in force carries no cloud layer, so sky.c draws no sky and
 * instead fills the whole view with the environment's colour (sky.c:320,
 * skyRender's !Clouds branch), 0 otherwise or with no environment.
 *
 * A replacement background composes over the remaster's own backdrop, and
 * under it that fill is not the remaster's: the remaster clears to black
 * before its background (measured on the console at the Dam). Read-only.
 */
int sl_wa_stage_flat_backdrop(void);
int sl_wa_stage_flat_backdrop(void)
{
    struct CurrentEnvironmentRecord *env = fogGetCurrentEnvironmentp();

    if (env == (struct CurrentEnvironmentRecord *) 0) return 0;
    return env->Clouds ? 0 : 1;
}
