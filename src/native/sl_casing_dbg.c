/**
 * Ejected-casing witness, native side. Inert unless SL_CASING_DBG is set.
 *
 * WHAT THIS IS FOR. "RC-P90 shell ejection looks weird" (owner, Jungle) is a
 * claim about g_Casings (gun.c:46): where a casing is spawned, how fast it
 * leaves, how it spins, how long it lives. A screenshot shows a brass speck;
 * this prints the record the game integrates (gunfire.c:5557
 * update_bullet_casing) so the native and the cartridge can be compared as
 * numbers at the same pose.
 *
 *   SL_CASING_DBG=1   sl_casing: f<frame> [<slot>] NEW|... pos=x,y,z vel=x,y,z
 *                     rot-scale=a,b,c floor=<y> model=<ptr>   (every frame a
 *                     slot is live, plus a GONE line when it is freed)
 *
 * READ-ONLY, like sl_mission_dbg.c: the array is the game's own, declared in
 * gun.h; nothing is written.
 */
#ifndef __sgi

#include <ultra64.h>
#include <bondgame.h>
#include "gun.h"

extern int    fprintf(void *, const char *, ...);
extern void  *stderr;
extern char  *getenv(const char *);
extern float  sqrtf(float);

#define SL_CASING_N 20   /* gun.c:46 */

static int g_cd_checked;
static int g_cd_on;
static void *g_cd_live[SL_CASING_N];

static float sl_cd_rowlen(const f32 *r)
{
    return sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
}

void sl_casing_probe_frame(s32 frame)
{
    int i;

    if (!g_cd_checked) {
        const char *e = getenv("SL_CASING_DBG");
        g_cd_on = (e != NULL && *e != '\0' && *e != '0');
        g_cd_checked = 1;
    }
    if (!g_cd_on)
        return;
    for (i = 0; i < SL_CASING_N; i++) {
        const CasingRecord *c = &g_Casings[i];
        if (c->header == NULL) {
            if (g_cd_live[i] != NULL) {
                fprintf(stderr, "sl_casing: f%d [%d] GONE\n", (int) frame, i);
                g_cd_live[i] = NULL;
            }
            continue;
        }
        fprintf(stderr, "sl_casing: f%d [%d] %s pos=%.2f,%.2f,%.2f vel=%.3f,%.3f,%.3f"
                        " rot-scale=%.3f,%.3f,%.3f floor=%.1f model=%p\n",
                (int) frame, i, g_cd_live[i] == NULL ? "NEW" : "...",
                (double) c->pos.x, (double) c->pos.y, (double) c->pos.z,
                (double) c->vel.x, (double) c->vel.y, (double) c->vel.z,
                (double) sl_cd_rowlen(c->rot_mtx.m[0]),
                (double) sl_cd_rowlen(c->rot_mtx.m[1]),
                (double) sl_cd_rowlen(c->rot_mtx.m[2]),
                (double) c->floor_y_pos, (void *) c->header);
        g_cd_live[i] = (void *) c->header;
    }
}

#endif /* __sgi */
