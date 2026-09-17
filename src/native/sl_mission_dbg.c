/**
 * Mission-progress probe, native side. Inert unless SL_MISSION_DBG is set.
 *
 * WHAT THIS IS FOR. Playing a level under bounded scripted input, the log is
 * the only witness that survives the run: a screenshot window shows what was
 * drawn, but whether the setup file's objectives INITIALISED, which one just
 * flipped, whether Bond died, and whether the level handed control back to
 * the front end are simulation facts - and until this file the only way to
 * read them was the HUD message ("Objective a: Completed") in a frame that
 * happened to be captured. The end-to-end chain this makes observable is
 * the one B-096 left unproven: objectives -> completion -> level exit ->
 * debrief -> next mission.
 *
 *   SL_MISSION_DBG=1        print on CHANGE only (objective table with each
 *                           objective's criteria and where their tagged
 *                           objects are, on the first frame it exists; then
 *                           every status flip, every KIA / abort / stage /
 *                           front-end menu edge)
 *   SL_MISSION_EVERY=<n>    additionally, an eye/look/room/health line
 *                           every n of this probe's own calls (0 = never;
 *                           default 0)
 *   SL_MISSION_MAP=1        once per level, the rooms' bounds, the portals
 *                           and the doors, in world units (sl_md_print_map)
 *                           - the route plan for a scripted walk
 *
 * WHY NATIVE, NOT src/game. Same argument as sl_cheat.c and sl_teleport.c:
 * src/game stays byte-identical to the matching build, src/platform cannot
 * see a game struct, and src/native is compiled with the game include path
 * but included by nothing in src/game. READ-ONLY: every value below is the
 * game's own, read through its own accessors where one exists
 * (get_status_of_objective, lvlGetSelectedDifficulty, bossGetStageNum,
 * get_currentmenu, getMissiontimer) and through the exported globals
 * front.h / objective_status.h already declare otherwise. Nothing is written.
 *
 * ONE PROBE, EVERY LEVEL. Nothing here knows which stage is loaded; the
 * objective list is whatever the setup file created (objective_count,
 * objective_status.c:29) and the difficulty gate is the game's own
 * (objectiveIsAllComplete's test, objective_status.c:323).
 */
#ifndef __sgi

#include <ultra64.h>
#include <bondgame.h>
#include <boss.h>
#include "bondview.h"
#include "player.h"
#include "objective_status.h"
#include "front.h"
#include "lv.h"
#include "gun.h"
#include "loadobjectmodel.h"
#include "bg.h"

extern int    fprintf(void *, const char *, ...);
extern void  *stderr;
extern char  *getenv(const char *);
extern int    atoi(const char *);

/* Declared in no header this TU can reach; signatures verbatim from their
 * definitions: objective_status.c:126, :138, front.c:8848. */
extern u8  *get_text_for_objective(int objectiveIndex);
extern s32  get_difficulty_for_objective(s32 objectiveIndex);
extern MENU get_currentmenu(void);
/* boss.c:162 - the stage the main loop will load next (LEVELID_NONE while
 * a level runs; set by bossSetLoadedStage when it ends). */
extern s32  g_MainStageNum;

#define SL_MD_SLOTS 10

static int g_md_checked;
static int g_md_on;
static int g_md_every;
static unsigned g_md_calls;

/* Shadows of what was last printed, so a line means "this changed". */
static s32 g_md_count = -2;             /* objective_count; -1 is "no level" */
static s32 g_md_status[SL_MD_SLOTS];
static s32 g_md_kia = -1;
static s32 g_md_aborted = -1;
static s32 g_md_stage = -2;
static s32 g_md_mainstage = -2;
static s32 g_md_menu = -2;
static s32 g_md_dead = -1;
static s32 g_md_cam = -2;

/* The game's text has its own control bytes (colour, font); print the
 * printable ASCII run and nothing else, bounded. */
static void sl_md_text(const u8 *s, char *out, int n)
{
    int i = 0;
    if (s != NULL) {
        while (*s != '\0' && i < n - 1) {
            if (*s >= 0x20 && *s < 0x7f) out[i++] = (char) *s;
            s++;
        }
    }
    out[i] = '\0';
}

static const char *sl_md_status_name(s32 st)
{
    switch (st) {
    case OBJECTIVESTATUS_INCOMPLETE: return "INCOMPLETE";
    case OBJECTIVESTATUS_COMPLETE:   return "COMPLETE";
    case OBJECTIVESTATUS_FAILED:     return "FAILED";
    default:                         return "?";
    }
}

static s32 sl_md_room(struct player *pl)
{
    if (pl == NULL)
        return -1;
    if (pl->cameramode == 1 && pl->cameratile != NULL)
        return (s32) pl->cameratile->room;
    if (pl->field_488.current_tile_ptr_for_portals == NULL)
        return -1;
    return (s32) pl->field_488.current_tile_ptr_for_portals->room;
}

static void sl_md_print_objective(s32 frame, s32 i, s32 st)
{
    char txt[96];
    s32 diff = get_difficulty_for_objective(i);
    sl_md_text(get_text_for_objective((int) i), txt, (int) sizeof txt);
    fprintf(stderr, "sl_mission: f%d objective %c diff>=%d %s%s \"%s\"\n",
            (int) frame, (int) ('a' + i), (int) diff, sl_md_status_name(st),
            diff <= lvlGetSelectedDifficulty() ? "" : " (not at this difficulty)",
            txt);
}

/* The criteria behind one objective, once, when the table first exists:
 * the setup file's sub-records between this objective's START and its END
 * (the walk get_status_of_objective makes, objective_status.c:179), and for
 * every tagged object, where it is right now - its prop's position and room,
 * and whether a character is carrying it (the prop's parent is a chr). This
 * is what a scripted run navigates by; nothing here is level-specific. */
static void sl_md_print_criteria(s32 frame, s32 i)
{
    MissionObjectiveRecord *o;
    if (i >= OBJECTIVES_MAX || objective_ptrs[i] == NULL)
        return;
    for (o = (MissionObjectiveRecord *) &objective_ptrs[i]->id;
         o->type != PROPDEF_OBJECTIVE_END;
         o = (MissionObjectiveRecord *) (sizepropdef((PropDefHeaderRecord *) o)
                                         + (PropDefHeaderRecord *) o)) {
        ObjectRecord *obj;
        PropRecord *pr;
        if (o->type == PROPDEF_OBJECTIVE_START)
            continue;
        /* COPY_ITEM (0x22) is a one-word record with no operand - the notes'
         * "copy item 00000022" - so its ObjRefID slot is the NEXT record. */
        if (o->type == PROPDEF_OBJECTIVE_COPY_ITEM)
            fprintf(stderr, "sl_mission: f%d   %c criterion type=0x%x (key analyser)",
                    (int) frame, (int) ('a' + i), (int) o->type);
        else
            fprintf(stderr, "sl_mission: f%d   %c criterion type=0x%x ref=%d",
                    (int) frame, (int) ('a' + i), (int) o->type, (int) o->ObjRefID);
        switch (o->type) {
        case PROPDEF_OBJECTIVE_DESTROY_OBJECT:
        case PROPDEF_OBJECTIVE_COLLECT_OBJECT:
        case PROPDEF_OBJECTIVE_DEPOSIT_OBJECT:
        case PROPDEF_OBJECTIVE_PHOTOGRAPH:
            obj = objFindByTagId(o->ObjRefID);
            pr = (obj != NULL) ? obj->prop : NULL;
            if (obj == NULL) {
                fprintf(stderr, " (tag resolves to no object)");
            } else if (pr == NULL) {
                fprintf(stderr, " obj=%d (no prop)", (int) obj->obj);
            } else {
                fprintf(stderr, " obj=%d pos=%.1f,%.1f,%.1f room=%d",
                        (int) obj->obj, (double) pr->pos.f[0],
                        (double) pr->pos.f[1], (double) pr->pos.f[2],
                        (int) pr->rooms[0]);
                if (pr->parent != NULL)
                    fprintf(stderr, " carried-by-prop-type=%d",
                            (int) pr->parent->type);
            }
            break;
        default:
            break;
        }
        fprintf(stderr, "\n");
    }
}

/* SL_MISSION_MAP=1: once per level, the navigable structure of the level as
 * the game holds it - every room's bounds (g_BgRoomInfo, bg.h:94), every
 * portal with the two rooms it joins and its centroid (g_BgPortals, walked
 * to the NULL terminator exactly as bg.c:1086 does), and every door prop
 * with its position and the rooms it stands in. A scripted run plans its
 * route from this rather than from anyone's memory of the level. */
static int g_md_map;
static s32 g_md_map_stage = -2;

static void sl_md_print_map(s32 frame, s32 stage)
{
    s32 i, n;
    f32 sc;
    PropRecord *pr;

    if (!g_md_map || stage == g_md_map_stage || stage == LEVELID_TITLE)
        return;
    if (g_MaxNumRooms <= 0 || g_BgPortals == NULL)
        return;
    g_md_map_stage = stage;

    /* Room bounds and portal points are stored in the background's own
     * units; the level's scale (bg.c:1204, 1/scale in room_data_float2) is
     * what bg.c:1705 applies to put a portal point in world units, so it is
     * applied here too and everything below is in the same space as the
     * props and the eye. */
    sc = get_room_data_float2();
    fprintf(stderr, "sl_map: f%d rooms=%d bg-to-world scale=%.4f\n",
            (int) frame, (int) g_MaxNumRooms, (double) sc);
    for (i = 0; i < g_MaxNumRooms; i++) {
        s_room_info *r = &g_BgRoomInfo[i];
        fprintf(stderr, "sl_map: f%d room %d min=%.0f,%.0f,%.0f max=%.0f,%.0f,%.0f\n",
                (int) frame, (int) i,
                (double) (r->minbounds.f[0] * sc), (double) (r->minbounds.f[1] * sc),
                (double) (r->minbounds.f[2] * sc), (double) (r->maxbounds.f[0] * sc),
                (double) (r->maxbounds.f[1] * sc), (double) (r->maxbounds.f[2] * sc));
    }
    for (i = 0; g_BgPortals[i].offset_portal != NULL; i++) {
        bg_portal_entry *pe = g_BgPortals[i].offset_portal;
        coord3d *pt = &pe->point;
        f32 cx = 0.0f, cy = 0.0f, cz = 0.0f;
        s32 k;
        for (k = 0; k < (s32) pe->numPoints; k++) {
            cx += pt[k].f[0]; cy += pt[k].f[1]; cz += pt[k].f[2];
        }
        if (pe->numPoints > 0) {
            cx = cx * sc / (f32) pe->numPoints;
            cy = cy * sc / (f32) pe->numPoints;
            cz = cz * sc / (f32) pe->numPoints;
        }
        fprintf(stderr, "sl_map: f%d portal %d rooms=%d,%d flags=%02x,%02x centre=%.0f,%.0f,%.0f\n",
                (int) frame, (int) i, (int) g_BgPortals[i].connectedRoom1,
                (int) g_BgPortals[i].connectedRoom2,
                (int) g_BgPortals[i].controlbytes1, (int) g_BgPortals[i].controlbytes2,
                (double) cx, (double) cy, (double) cz);
    }
    n = 0;
    for (pr = g_ActivePropsTail; pr != NULL && n < 4096; pr = pr->prev, n++) {
        if (pr->type == PROP_TYPE_DOOR && pr->door != NULL) {
            DoorRecord *d = pr->door;
            fprintf(stderr, "sl_map: f%d door pos=%.0f,%.0f,%.0f rooms=%d,%d pad=%d flags=%08x flags2=%08x doorflags=%02x key=%d\n",
                    (int) frame, (double) pr->pos.f[0], (double) pr->pos.f[1],
                    (double) pr->pos.f[2], (int) pr->rooms[0], (int) pr->rooms[1],
                    (int) ((ObjectRecord *) d)->pad,
                    (unsigned) ((ObjectRecord *) d)->flags,
                    (unsigned) ((ObjectRecord *) d)->flags2,
                    (int) d->doorFlags, (int) d->keyflags);
        }
    }
}

/* Called once per pumped frame from the shim (src/platform/sl_ultra_shim.c),
 * beside sl_cheat_poll. */
void sl_mission_probe_frame(s32 frame)
{
    struct player *pl;
    s32 stage, mainstage, i;

    if (!g_md_checked) {
        const char *e = getenv("SL_MISSION_DBG");
        g_md_checked = 1;
        g_md_on = (e != NULL && *e != '\0' && *e != '0');
        e = getenv("SL_MISSION_EVERY");
        g_md_every = (e != NULL) ? atoi(e) : 0;
        e = getenv("SL_MISSION_MAP");
        g_md_map = (e != NULL && *e != '\0' && *e != '0');
    }
    if (!g_md_on)
        return;
    g_md_calls++;

    stage = (s32) bossGetStageNum();
    mainstage = (s32) g_MainStageNum;
    if (stage != g_md_stage || mainstage != g_md_mainstage) {
        fprintf(stderr, "sl_mission: f%d stage=%d main=%d difficulty=%d"
                        " menu-difficulty=%d briefingpage=%d\n",
                (int) frame, (int) stage, (int) mainstage,
                (int) lvlGetSelectedDifficulty(), (int) selected_difficulty,
                (int) briefingpage);
        g_md_stage = stage;
        g_md_mainstage = mainstage;
    }

    /* The front end: which menu is up. Only meaningful on the title stage,
     * where the debrief (MENU_MISSION_FAILED / MENU_MISSION_COMPLETE) and the
     * next briefing are decided (front.c interface_menu0D_missioncomplete). */
    if (stage == LEVELID_TITLE) {
        s32 menu = (s32) get_currentmenu();
        if (menu != g_md_menu) {
            fprintf(stderr, "sl_mission: f%d menu=%d kia=%d aborted=%d"
                            " briefingpage=%d selected_stage=%d\n",
                    (int) frame, (int) menu, (int) g_isBondKIA,
                    (int) mission_failed_or_aborted, (int) briefingpage,
                    (int) selected_stage);
            g_md_menu = menu;
        }
    } else {
        g_md_menu = -2;
    }

    if (g_isBondKIA != g_md_kia || mission_failed_or_aborted != g_md_aborted) {
        fprintf(stderr, "sl_mission: f%d kia=%d aborted=%d timer=%d\n",
                (int) frame, (int) g_isBondKIA, (int) mission_failed_or_aborted,
                (int) getMissiontimer());
        g_md_kia = g_isBondKIA;
        g_md_aborted = mission_failed_or_aborted;
    }

    /* Objectives: the whole table when it first exists (or is rebuilt for
     * another level), then each flip. objective_count is -1 with no level
     * loaded and the setup file's count-1 otherwise. */
    if (objective_count != g_md_count) {
        g_md_count = objective_count;
        fprintf(stderr, "sl_mission: f%d objectives=%d at difficulty %d\n",
                (int) frame, (int) (objective_count + 1),
                (int) lvlGetSelectedDifficulty());
        for (i = 0; i < SL_MD_SLOTS; i++)
            g_md_status[i] = -1;
        sl_md_print_map(frame, stage);
    }
    if (objective_count >= 0 && stage != LEVELID_TITLE) {
        for (i = 0; i <= objective_count && i < SL_MD_SLOTS; i++) {
            s32 st = (s32) get_status_of_objective(i);
            if (st != g_md_status[i]) {
                sl_md_print_objective(frame, i, st);
                if (g_md_status[i] == -1)
                    sl_md_print_criteria(frame, i);
                g_md_status[i] = st;
            }
        }
    }

    pl = g_CurrentPlayer;
    if (pl == NULL || stage == LEVELID_TITLE)
        return;
    if ((s32) pl->bonddead != g_md_dead || (s32) pl->cameramode != g_md_cam) {
        fprintf(stderr, "sl_mission: f%d dead=%d cameramode=%d health=%.3f armour=%.3f\n",
                (int) frame, (int) pl->bonddead, (int) pl->cameramode,
                (double) pl->bondhealth, (double) pl->bondarmour);
        g_md_dead = (s32) pl->bonddead;
        g_md_cam = (s32) pl->cameramode;
    }
    if (g_md_every > 0 && (g_md_calls % (unsigned) g_md_every) == 0
        && pl->viewtoworldmtxf != NULL) {
        /* vv_theta / vv_verta are the look angles the mark-teleport restores
         * (sl_teleport.c:151): theta in degrees, 0 = +z, look = (-sin, 0, cos). */
        fprintf(stderr, "sl_mission: f%d eye=%.1f,%.1f,%.1f theta=%.1f verta=%.1f room=%d"
                        " health=%.3f weapon=%d timer=%d\n",
                (int) frame,
                (double) pl->viewtoworldmtxf->m[3][0],
                (double) pl->viewtoworldmtxf->m[3][1],
                (double) pl->viewtoworldmtxf->m[3][2],
                (double) pl->vv_theta, (double) pl->vv_verta,
                (int) sl_md_room(pl), (double) pl->bondhealth,
                (int) getCurrentPlayerWeaponId(GUNRIGHT),
                (int) getMissiontimer());
    }
}

#endif /* __sgi */
