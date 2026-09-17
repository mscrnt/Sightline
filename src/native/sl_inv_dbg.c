/**
 * Inventory witness, native side. Inert unless SL_INV_DBG is set.
 *
 * WHAT THIS IS FOR. "I can't pick up the remote mines" (Control, owner mark
 * 20260915-204906/mark-001) is a claim about INVENTORY, and until this file
 * nothing in a bounded run could witness it: the mission probe prints the
 * right-hand weapon, a screenshot window shows the HUD message if one was
 * drawn on a captured frame, and neither says whether the item entered the
 * cycle list or how much ammo of its type Bond holds. The pickup path is
 * propobj.c propPickupByPlayer -> add_ammo_to_inventory ->
 * bondinvAddInvItem / give_cur_player_ammo, and this prints the two things
 * it mutates, on change only:
 *
 *   SL_INV_DBG=1    sl_inv: f<frame> items=[w4 w29 w30 ...] held=<n>
 *                   sl_inv: f<frame> ammo[<type>] <old> -> <new>
 *
 * `w<n>` is INV_ITEM_WEAPON with its ITEM_IDS number, `d<r>+<l>` a dual,
 * `p` a prop item; `held` is the count of cycle entries. Ammo is
 * g_CurrentPlayer->ammoheldarr by AMMOTYPE (07 remote mines, per
 * "Objects and Attributes/ammunition/ammo types and amounts.txt").
 *
 * WHY NATIVE, NOT src/game. Same argument as sl_mission_dbg.c: src/game
 * stays byte-identical to the matching build, src/platform cannot see a game
 * struct, and src/native is compiled with the game include path but included
 * by nothing in src/game. READ-ONLY: it walks the game's own cycle list
 * (bondinv.c:191, the same walk bondinvGetInvItem makes) and reads the ammo
 * array the game's own accessor returns. Nothing is written.
 */
#ifndef __sgi

#include <ultra64.h>
#include <bondgame.h>
#include <boss.h>
#include "bondview.h"
#include "player.h"
#include "bondinv.h"

extern int    fprintf(void *, const char *, ...);
extern void  *stderr;
extern char  *getenv(const char *);

#define SL_INV_AMMO 30          /* gun.c:520 AMMO_RELATED_MAX, the array's own extent */
#define SL_INV_SIG  64

static int g_inv_checked;
static int g_inv_on;
static s32 g_inv_ammo[SL_INV_AMMO];
static int g_inv_ammo_init;
static s32 g_inv_sig[SL_INV_SIG];
static int g_inv_sig_n = -1;

void sl_inv_probe_frame(s32 frame)
{
    struct player *pl;
    s32 sig[SL_INV_SIG];
    int n = 0, i, changed;
    InvItem *first, *it;

    if (!g_inv_checked) {
        const char *e = getenv("SL_INV_DBG");
        g_inv_on = (e != NULL && *e != '\0' && *e != '0');
        g_inv_checked = 1;
    }
    if (!g_inv_on)
        return;
    pl = g_CurrentPlayer;
    if (pl == NULL || pl->prop == NULL)
        return;

    /* The cycle list, as bondinvGetInvItem walks it (bondinv.c:191). */
    first = pl->ptr_inventory_first_in_cycle;
    it = first;
    while (it != NULL && n + 3 <= SL_INV_SIG) {
        sig[n++] = it->type;
        if (it->type == INV_ITEM_WEAPON) {
            sig[n++] = it->type_inv_item.type_weap.weapon;
        } else if (it->type == INV_ITEM_DUAL) {
            sig[n++] = it->type_inv_item.type_dual.weapon_right;
            sig[n++] = it->type_inv_item.type_dual.weapon_left;
        } else {
            sig[n++] = 0;
        }
        it = it->next;
        if (it == first)
            break;
    }
    changed = (n != g_inv_sig_n);
    for (i = 0; !changed && i < n; i++)
        if (sig[i] != g_inv_sig[i]) changed = 1;
    if (changed) {
        int held = 0;
        fprintf(stderr, "sl_inv: f%d items=[", (int) frame);
        for (i = 0; i < n; ) {
            s32 t = sig[i++];
            if (t == INV_ITEM_WEAPON) {
                fprintf(stderr, "%sw%d", held ? " " : "", (int) sig[i++]);
            } else if (t == INV_ITEM_DUAL) {
                fprintf(stderr, "%sd%d+%d", held ? " " : "", (int) sig[i], (int) sig[i + 1]);
                i += 2;
            } else {
                fprintf(stderr, "%s%s", held ? " " : "", t == INV_ITEM_PROP ? "p" : "?");
                i++;
            }
            held++;
        }
        fprintf(stderr, "] held=%d\n", held);
        for (i = 0; i < n; i++) g_inv_sig[i] = sig[i];
        g_inv_sig_n = n;
    }

    /* Ammo by type, the array give_cur_player_ammo writes (gunfire.c:5785). */
    for (i = 0; i < SL_INV_AMMO; i++) {
        s32 v = pl->ammoheldarr[i];
        if (!g_inv_ammo_init) {
            g_inv_ammo[i] = v;
        } else if (v != g_inv_ammo[i]) {
            fprintf(stderr, "sl_inv: f%d ammo[%d] %d -> %d\n",
                    (int) frame, i, (int) g_inv_ammo[i], (int) v);
            g_inv_ammo[i] = v;
        }
    }
    if (!g_inv_ammo_init) {
        g_inv_ammo_init = 1;
        fprintf(stderr, "sl_inv: f%d ammo", (int) frame);
        for (i = 0; i < SL_INV_AMMO; i++)
            if (pl->ammoheldarr[i] != 0)
                fprintf(stderr, " [%d]=%d", i, (int) pl->ammoheldarr[i]);
        fprintf(stderr, "\n");
    }
}

#endif /* __sgi */
