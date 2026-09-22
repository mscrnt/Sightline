/**
 * sl_world_detail.c - the WORLD DETAIL ENHANCED profile's render-only prop
 * lift (#43, 2026-09-21, the owner's Dam yard marks).
 *
 * THE MECHANISM IT RELAXES. The object tick (src/game/propobj.c objTick,
 * :5948) decides a prop's PROPFLAG_ONSCREEN through posIsOnScreen, whose
 * third gate is sub_GAME_7F054C58 - the near-fog visibility-range formula
 * (NearFog / MaxVisRange / MaxObfuscationRange, size-scaled, times
 * c_lodscalez) evaluated on the TICK side. A prop it rejects never gets the
 * flag, never gets render matrices, never enters g_OnScreenPropList and so
 * never reaches chrobjRenderProp - which is why the render seam's ENHANCED
 * arm (chrobjFogVisRangeRelated) could not lift it. MEASURED at the owner's
 * mark 1 (Dam 15227 / 60.3 / 13970.6 theta 212.4, room 110, the tunnel mouth
 * onto the yard): 9 of the yard's 24 crates (PROPDEF_PROP) in the rendered
 * room 111 read fogvis=1 inbox=1 visrange=0 -> off screen; 600 units on
 * (mark 2) all 24 pass. The owner's [submitted-lists] agree: 7 crate lists
 * at mark 1, 16 at mark 2.
 *
 * THE BOUNDARY, and why this is a LIFT and not a widening. PROPFLAG_ONSCREEN
 * is simulation state: it is hashed by the trace (sl_state_reader.c hashes
 * prop->flags), read by the AI (chrai.c / aicommands.def IF-I'M-ON-SCREEN),
 * and it is the membership test of g_OnScreenPropList - the list a shot
 * (chraiDefaultWeaponFireHandler), a punch, INTERACT (propFindForInteract)
 * and auto-aim walk. None of that may move. So a lifted prop is drawn
 * WITHOUT the flag and WITHOUT joining that list: it is a render-only
 * eligibility kept in this file's own per-frame list, rebuilt every
 * propsTick and walked by chrpropsRenderPass after the on-screen props of
 * the same room and pass. CONSEQUENCE the owner must weigh: a lifted prop
 * is visible but, until it comes inside the cartridge's own range, it is
 * exactly what it is under ORIGINAL - not shootable, not interactable, not
 * a target; a bullet passes through it to whatever the cartridge would have
 * hit. The alternative - lifting the flag - would change the simulation.
 *
 * ELIGIBILITY, derived from the tick: the prop must be one posIsOnScreen
 * admits with the visibility-range term alone removed (the same call with
 * applyFogCull = FALSE: rendered room, fog-visible, inside its rooms'
 * portal window), and its on-screen matrix path must be the GENERIC one
 * (obj->mtx + runtime_pos through the world-to-screen), so the game's own
 * code builds the matrices with no special-case side effect: doors, CCTVs,
 * autoguns, vehicles, aircraft and tanks are excluded (their branches carry
 * sounds, collision and per-part state) and stay ORIGINAL.
 *
 * WHAT THE LIFTED PATH DOES AND DOES NOT DO (the islands in objTick): it
 * takes the on-screen branch for the matrices, prop->zDepth and the model's
 * relations (render-side data, none of it hashed), but it does not set
 * PROPFLAG_ONSCREEN, does not run update_color_shading (the sim owner's
 * per-frame shade lerp stays exactly ORIGINAL's), and its children take the
 * off-screen child tick as they would have. The trace over the dam-pad
 * stream is byte-identical under both profiles (docs/backlog.md).
 */
#ifndef __sgi

#include <ultra64.h>
#include <bondgame.h>
#include "chrai.h"
#include "propobj.h"
#include "../platform/sl_settings.h"

extern int sl_world_detail_enhanced(void);

#define SL_LIFT_MAX 128

static PropRecord *s_lift[SL_LIFT_MAX];
static int         s_lift_n;
static int         s_lift_overflow;
static unsigned    s_lift_frames, s_lift_total;   /* the census: frames with a lift, props lifted */

/* Called at the head of propsTick: this frame's list starts empty. */
void sl_world_detail_lift_reset(void)
{
    s_lift_n = 0;
    s_lift_overflow = 0;
}

/* Is this object's on-screen matrix path the generic one? The special
 * branches of objTick's on-screen arm, by object type. */
static int sl_lift_generic_type(u8 type)
{
    switch (type) {
    case PROPDEF_DOOR:
    case PROPDEF_DOOR_SCALE:
    case PROPDEF_CCTV:
    case PROPDEF_AUTOGUN:
    case PROPDEF_VEHICHLE:
    case PROPDEF_AIRCRAFT:
    case PROPDEF_TANK:
        return 0;
    default:
        return 1;
    }
}

/* The tick asks: this prop failed posIsOnScreen with the visibility-range
 * term applied - is it render-eligible without it? 1 = lift (the caller
 * builds the render data through the on-screen arm, flag untouched). */
int sl_world_detail_lift_query(PropRecord *prop, coord3d *pos, f32 size)
{
    if (!sl_world_detail_enhanced())
        return 0;
    if (prop->obj == NULL || !sl_lift_generic_type((u8) prop->obj->type))
        return 0;
    if (!posIsOnScreen(prop, pos, size, FALSE))
        return 0;
    if (s_lift_n >= SL_LIFT_MAX) { s_lift_overflow++; return 0; }
    s_lift[s_lift_n++] = prop;
    s_lift_total++;
    return 1;
}

/* Is the prop in this frame's lift list? The render path's replacement for
 * the PROPFLAG_ONSCREEN test on the lifted props. */
int sl_world_detail_lifted(PropRecord *prop)
{
    int i;
    for (i = 0; i < s_lift_n; i++)
        if (s_lift[i] == prop)
            return 1;
    return 0;
}

/* chrpropsRenderPass: after the room's on-screen props of this pass, the
 * lifted props of the same room, farthest first (the alpha pass draws back
 * to front, and a lifted prop is by construction beyond the range - so
 * before the on-screen set is the right order for the opaque pass too,
 * where depth decides). The room and pass filters are the pass's own. */
extern Gfx *chrpropRender(Gfx *gdl, PropRecord *prop, s32 withalpha);
extern void chraiGetPropRoomIds(PropRecord *self, s32 *roomids);
extern u8   getROOMID_isRendered(s32 roomID);

Gfx *sl_world_detail_lift_render(Gfx *gdl, s32 roomid, s32 renderpass)
{
    PropRecord *order[SL_LIFT_MAX];
    s32 rooms[PROPRECORD_STAN_ROOM_LEN + 2];
    int i, j, n = 0;

    if (s_lift_n == 0)
        return gdl;
    /* far to near by zDepth (insertion sort; the list is small) */
    for (i = 0; i < s_lift_n; i++) {
        PropRecord *p = s_lift[i];
        for (j = n; j > 0 && order[j - 1]->zDepth < p->zDepth; j--)
            order[j] = order[j - 1];
        order[j] = p;
        n++;
    }
    for (i = 0; i < n; i++) {
        PropRecord *prop = order[i];
        s32 *rp;
        int inroom = 0, pass_ok = 0;
        if (renderpass == 0)
            pass_ok = (prop->flags & (PROPFLAG_00000020 | PROPFLAG_RENDERPOSTBG)) == 0;
        else if (renderpass == 2)
            pass_ok = (prop->flags & (PROPFLAG_00000020 | PROPFLAG_RENDERPOSTBG)) == PROPFLAG_RENDERPOSTBG;
        else
            pass_ok = 1;
        if (!pass_ok)
            continue;
        chraiGetPropRoomIds(prop, rooms);
        for (rp = rooms; *rp >= 0; rp++) {
            if (getROOMID_isRendered(*rp)) {
                inroom = (roomid == *rp);
                break;
            }
        }
        if (!inroom)
            continue;
        if (renderpass == 0 || renderpass == 2) {
            gdl = chrpropRender(gdl, prop, 0);
        } else {
            if (prop->flags & PROPFLAG_00000020)
                gdl = chrpropRender(gdl, prop, 0);
            gdl = chrpropRender(gdl, prop, 1);
        }
    }
    return gdl;
}

/* The census, for the per-frame heartbeat: props lifted this frame and
 * whether the list overflowed. */
int sl_world_detail_lift_count(int *overflow)
{
    if (overflow != NULL) *overflow = s_lift_overflow;
    return s_lift_n;
}

#endif /* __sgi */
