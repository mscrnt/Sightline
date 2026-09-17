/**
 * Developer-only cheat and starting-weapon selection, native side.
 *
 * WHAT THIS IS FOR. A renderer bug in a weapon the level does not hand Bond
 * at spawn - Surface's sniper rifle scope (Gitea #17) lives in a hut minutes
 * into the level - cannot be witnessed at a direct boot without a way to be
 * holding that weapon at frame one. Replaying the owner's session is not that
 * way (the replay drifts; sl_teleport.c records why). So, like the teleport:
 * boot the level, stand at the mark, and HOLD THE WEAPON, all from the
 * environment play.ps1 fills in.
 *
 *   SL_CHEATS=<name,name,...>   activate GoldenEye's own cheats for the level
 *   SL_WEAPON=<name|number>     put that item in Bond's right hand at spawn
 *   SL_STAGE_FLAGS=<hex>[@<n>]  raise those bits of the level's stage-flag
 *                               register n frames after first person (default
 *                               120) - the register the setup's AI lists test
 *                               with if_objective_bitfield_is_set_on, so a
 *                               scripted outro (Aztec's ai_16: bit 0x4000 ->
 *                               fade, camera 0x16, object_rocket_launch) can
 *                               be reached without playing the level to it.
 *                               MEASURED 2026-09-17: the generated setup
 *                               source (assets/obseg/setup/UsetupaztZ.c)
 *                               prints that operand as 0x400000 - one byte
 *                               left of the value the command's QBYTE
 *                               operand carries (0x400000 raised nothing;
 *                               0x4000 ran the outro at the owner's mark
 *                               pose, eye -3555.1 405.2 1813.7 room 66)
 *
 * WHY NATIVE, NOT src/game. Same argument as sl_teleport.c: src/game stays
 * byte-identical to the matching build, src/platform cannot see a game
 * struct, and src/native is compiled with the game include path but included
 * by nothing in src/game, so `make check-layering` is untouched. Everything
 * here goes through the game's OWN state and calls, never behind its back:
 *
 *   - Cheats are exactly what the cheat menu leaves behind. front.c:7959
 *     toggles g_CheatActivated[id] and update_menu15_cheat (front.c:7842)
 *     derives g_AppendCheatSinglePlayer from the array; lv.c:1012-1025 then
 *     calls cheatButtonTurnOnCheatForPlayers for every activated cheat on
 *     the level's FIRST ticking frame only - the gate is D_80048394 == 0,
 *     the level clock lv.c:1213 accumulates from then on (measured
 *     2026-09-15: a cheat word written after that tick never applies on
 *     the cartridge; this file writes it as soon as the player exists,
 *     before it). This file writes the array and calls the same deriver;
 *     the game applies the cheats itself. The HUD message ("All Guns On")
 *     is the cartridge's, and so is the consequence that a cheated mission
 *     is not recorded as completed (file.c:42). So is Max Ammo's: it fills
 *     every ammo type once (gunfire.c:5820), and a 0x14 ammo crate whose
 *     types are all at maximum is not collectable (propobj.c:11304, US) -
 *     a mines-only crate, and the mines it would grant, are out of reach
 *     for the rest of the level (Gitea #28).
 *
 *   - The weapon goes through the inventory and the hand-change REQUEST the
 *     next-weapon key uses (gun.c:1104 advance_through_inventory ->
 *     gunRequestHandWeaponChange), so the draw animation, ammo display and
 *     hand state are the game's. An item the level has not given is added
 *     with bondinvAddInvItem - the pickup path - and loaded through
 *     add_ammo_to_weapon with the weapon's own maximum (gunfire.c:5874,
 *     :5878). The request is made only once the camera is in first person,
 *     because that transition is where the game equips the level's starting
 *     weapons (bondview2.c:847-872) and a request made before it would be
 *     overwritten by it.
 *
 *   - The stage flags go through chrSetStageFlags (chraction.c:9913), the
 *     function the AI command objective_bitfield_set_on itself calls, so
 *     the register (objectiveregisters1, chr.c:1075) is written exactly the
 *     way a setup list writes it; the lists then react on their own tick.
 *
 * All three are inert unless named: one getenv each on the first frame, an
 * int test forever after.
 */
#ifndef __sgi

#include <ultra64.h>
#include <bondgame.h>
#include <boss.h>
#include "bondview.h"
#include "player.h"
#include "bondinv.h"
#include "gun.h"
#include "front.h"

extern int    fprintf(void *, const char *, ...);
extern void  *stderr;
extern char  *getenv(const char *);

/* Local, because include/string.h declares strlen over unsigned char and
 * the two prototypes would collide in this TU. */
static unsigned sl_len(const char *s)
{
    unsigned n = 0;
    while (s[n] != '\0') n++;
    return n;
}

static int sl_eq(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b) { a++; b++; }
    return *a == *b;
}

/* Declared in no header this TU can reach; signatures verbatim from their
 * definitions: gun.c:1048, gunfire.c:5878, front.c:7842. */
extern void update_menu15_cheat(void);
extern void gunRequestHandWeaponChange(enum GUNHAND hand, s32 nextWeapon, s32 cycleDirection);
extern s32  get_max_ammo_for_weapon(enum ITEM_IDS weapon);

struct sl_name_id { const char *name; int id; };

/* The cheats a single-player level can carry, by the name the game's own
 * menu shows them under (lower-cased, spaces dropped) and by the decomp's
 * enum stem. Ids are the CHEAT_IDS constants, never literals. The unlock-*
 * and level-select entries are menu state, not level effects, and are not
 * listed. */
static const struct sl_name_id g_cheat_names[] = {
    { "invincible",      CHEAT_INVINCIBILITY },
    { "invincibility",   CHEAT_INVINCIBILITY },
    { "allguns",         CHEAT_ALLGUNS },
    { "maxammo",         CHEAT_MAXAMMO },
    { "maximumammo",     CHEAT_MAXAMMO },
    { "linemode",        CHEAT_LINEMODE },
    { "2xhealth",        CHEAT_2X_HEALTH },
    { "2xarmor",         CHEAT_2X_ARMOR },
    { "2xarmour",        CHEAT_2X_ARMOR },
    { "invisible",       CHEAT_INVISIBILITY },
    { "invisibility",    CHEAT_INVISIBILITY },
    { "infammo",         CHEAT_INFINITE_AMMO },
    { "infiniteammo",    CHEAT_INFINITE_AMMO },
    { "dkmode",          CHEAT_DK_MODE },
    { "extraweapons",    CHEAT_EXTRA_WEAPONS },
    { "tinybond",        CHEAT_TINY_BOND },
    { "tiny",            CHEAT_TINY_BOND },
    { "paintball",       CHEAT_PAINTBALL },
    { "10xhealth",       CHEAT_10X_HEALTH },
    { "magnum",          CHEAT_MAGNUM },
    { "laser",           CHEAT_LASER },
    { "goldengun",       CHEAT_GOLDEN_GUN },
    { "silverpp7",       CHEAT_SILVER_PP7 },
    { "goldpp7",         CHEAT_GOLD_PP7 },
    { "bondphase",       CHEAT_BONDPHASE },
    { "noradar",         CHEAT_NO_RADAR_MP },
    { "turbo",           CHEAT_TURBO_MODE },
    { "turbomode",       CHEAT_TURBO_MODE },
    { "debugpos",        CHEAT_DEBUG_POS },
    { "fastanimation",   CHEAT_FAST_ANIMATION },
    { "slowanimation",   CHEAT_SLOW_ANIMATION },
    { "enemyrockets",    CHEAT_ENEMY_ROCKETS },
    { "2xrocketlauncher", CHEAT_2X_ROCKET_LAUNCHER },
    { "2xgrenadelauncher", CHEAT_2X_GRENADE_LAUNCHER },
    { "2xrcp90",         CHEAT_2X_RCP90 },
    { "2xthrowingknife", CHEAT_2X_THROWING_KNIFE },
    { "2xhuntingknife",  CHEAT_2X_HUNTING_KNIFE },
    { "2xlaser",         CHEAT_2X_LASER },
    { NULL, 0 }
};

/* Hand-held items, by the in-game name and the ITEM_IDS stem. Ids are the
 * ITEM_IDS constants. Everything below ITEM_BOMBCASE is a hand weapon in
 * bondinv.c's own test (bondinvItemAvailable, :260). */
static const struct sl_name_id g_item_names[] = {
    { "unarmed",        ITEM_UNARMED },
    { "fist",           ITEM_FIST },
    { "knife",          ITEM_KNIFE },
    { "huntingknife",   ITEM_KNIFE },
    { "throwknife",     ITEM_THROWKNIFE },
    { "throwingknife",  ITEM_THROWKNIFE },
    { "pp7",            ITEM_WPPK },
    { "wppk",           ITEM_WPPK },
    { "pp7sil",         ITEM_WPPKSIL },
    { "pp7silenced",    ITEM_WPPKSIL },
    { "wppksil",        ITEM_WPPKSIL },
    { "dd44",           ITEM_TT33 },
    { "tt33",           ITEM_TT33 },
    { "klobb",          ITEM_SKORPION },
    { "skorpion",       ITEM_SKORPION },
    { "kf7",            ITEM_AK47 },
    { "kf7soviet",      ITEM_AK47 },
    { "ak47",           ITEM_AK47 },
    { "zmg",            ITEM_UZI },
    { "uzi",            ITEM_UZI },
    { "d5k",            ITEM_MP5K },
    { "mp5k",           ITEM_MP5K },
    { "d5ksil",         ITEM_MP5KSIL },
    { "d5ksilenced",    ITEM_MP5KSIL },
    { "mp5ksil",        ITEM_MP5KSIL },
    { "phantom",        ITEM_SPECTRE },
    { "spectre",        ITEM_SPECTRE },
    { "ar33",           ITEM_M16 },
    { "m16",            ITEM_M16 },
    { "rcp90",          ITEM_FNP90 },
    { "fnp90",          ITEM_FNP90 },
    { "shotgun",        ITEM_SHOTGUN },
    { "autoshotgun",    ITEM_AUTOSHOT },
    { "autoshot",       ITEM_AUTOSHOT },
    { "sniper",         ITEM_SNIPERRIFLE },
    { "sniperrifle",    ITEM_SNIPERRIFLE },
    { "cougar",         ITEM_RUGER },
    { "cougarmagnum",   ITEM_RUGER },
    { "ruger",          ITEM_RUGER },
    { "goldengun",      ITEM_GOLDENGUN },
    { "silverpp7",      ITEM_SILVERWPPK },
    { "silverwppk",     ITEM_SILVERWPPK },
    { "goldpp7",        ITEM_GOLDWPPK },
    { "goldwppk",       ITEM_GOLDWPPK },
    { "laser",          ITEM_LASER },
    { "watchlaser",     ITEM_WATCHLASER },
    { "grenadelauncher", ITEM_GRENADELAUNCH },
    { "grenadelaunch",  ITEM_GRENADELAUNCH },
    { "rocketlauncher", ITEM_ROCKETLAUNCH },
    { "rocketlaunch",   ITEM_ROCKETLAUNCH },
    { "grenade",        ITEM_GRENADE },
    { "timedmine",      ITEM_TIMEDMINE },
    { "proximitymine",  ITEM_PROXIMITYMINE },
    { "remotemine",     ITEM_REMOTEMINE },
    { "detonator",      ITEM_TRIGGER },
    { "trigger",        ITEM_TRIGGER },
    { "taser",          ITEM_TASER },
    { "tankshells",     ITEM_TANKSHELLS },
    { NULL, 0 }
};

/* Match one token against a table, case-insensitively, ignoring '_', '-',
 * ' ' and an "ITEM_" / "CHEAT_" prefix, so "sniper", "SniperRifle",
 * "ITEM_SNIPERRIFLE" and "sniper-rifle" all name the same thing. A token
 * that is all digits is taken as the id itself. Returns -1 when unknown. */
static int sl_name_lookup(const struct sl_name_id *tab, const char *tok,
                          unsigned len, const char *prefix)
{
    char buf[40];
    unsigned i, n = 0, plen = sl_len(prefix);
    int digits = 1, val = 0;
    const struct sl_name_id *e;

    for (i = 0; i < len && n < sizeof buf - 1; i++) {
        char c = tok[i];
        if (c == '_' || c == '-' || c == ' ') continue;
        if (c >= 'A' && c <= 'Z') c = (char) (c - 'A' + 'a');
        if (c < '0' || c > '9') digits = 0; else val = val * 10 + (c - '0');
        buf[n++] = c;
    }
    buf[n] = '\0';
    if (n == 0) return -1;
    if (digits) return val;
    for (i = 0; i < plen; i++)
        if (buf[i] != prefix[i]) break;
    if (i == plen && n > plen) {
        /* "item_sniperrifle" -> "sniperrifle" (the '_' was already dropped,
         * so the prefix here is its bare stem). */
        for (i = 0; i + plen <= n; i++) buf[i] = buf[i + plen];
        n -= plen;
    }
    for (e = tab; e->name != NULL; e++)
        if (sl_eq(e->name, buf)) return e->id;
    return -1;
}

#define SL_CHEAT_MAX 16
static int g_sc_checked;
static int g_sc_cheat_n;
static int g_sc_cheats[SL_CHEAT_MAX];
static int g_sc_cheats_done;
static int g_sc_weapon = -1;           /* ITEM_IDS, or -1 for none      */
static int g_sc_weapon_done;
static int g_sc_fp_frames;             /* frames seen in FP so far       */
static u32 g_sc_flags;                 /* SL_STAGE_FLAGS bits, 0 = none  */
static int g_sc_flags_at = 120;        /* FP frames before they are raised */
static int g_sc_flags_done;
static int g_sc_flags_fp_frames;

/* chraction.c:9913 - the AI command objective_bitfield_set_on's own body. */
extern void chrSetStageFlags(ChrRecord *self, s32 flags);
extern s32  objectiveregisters1;

static void sl_cheat_parse_flags(const char *e)
{
    u32 v = 0;
    int at = 120, any = 0;
    if (e[0] == '0' && (e[1] == 'x' || e[1] == 'X')) e += 2;
    while (*e != '\0' && *e != '@') {
        char c = *e++;
        unsigned d;
        if (c >= '0' && c <= '9') d = (unsigned) (c - '0');
        else if (c >= 'a' && c <= 'f') d = (unsigned) (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = (unsigned) (c - 'A' + 10);
        else { any = 0; break; }
        v = (v << 4) | d; any = 1;
    }
    if (*e == '@') {
        at = 0; e++;
        while (*e >= '0' && *e <= '9') at = at * 10 + (*e++ - '0');
    }
    if (!any || v == 0) {
        fprintf(stderr, "sl_cheat: SL_STAGE_FLAGS needs <hex>[@<frames>] - ignored\n");
        return;
    }
    g_sc_flags = v;
    g_sc_flags_at = at;
}

static void sl_cheat_check_env(void)
{
    const char *e;
    g_sc_checked = 1;

    e = getenv("SL_CHEATS");
    if (e != NULL && *e != '\0') {
        const char *p = e;
        while (*p != '\0') {
            const char *q = p;
            int id;
            while (*q != '\0' && *q != ',') q++;
            id = sl_name_lookup(g_cheat_names, p, (unsigned) (q - p), "cheat");
            if (id <= 0 || id >= CHEAT_INVALID) {
                fprintf(stderr, "sl_cheat: unknown cheat '%.*s' ignored\n",
                        (int) (q - p), p);
            } else if (g_sc_cheat_n < SL_CHEAT_MAX) {
                g_sc_cheats[g_sc_cheat_n++] = id;
            }
            p = (*q == ',') ? q + 1 : q;
        }
    }

    e = getenv("SL_WEAPON");
    if (e != NULL && *e != '\0') {
        int id = sl_name_lookup(g_item_names, e, sl_len(e), "item");
        if (id < 0 || id >= ITEM_BOMBCASE)
            fprintf(stderr, "sl_cheat: unknown weapon '%s' ignored\n", e);
        else
            g_sc_weapon = id;
    }

    e = getenv("SL_STAGE_FLAGS");
    if (e != NULL && *e != '\0')
        sl_cheat_parse_flags(e);
}

/* Called once per pumped frame from the shim (src/platform/sl_ultra_shim.c),
 * beside sl_teleport_poll. */
void sl_cheat_poll(void)
{
    struct player *pl;

    if (!g_sc_checked)
        sl_cheat_check_env();
    if ((g_sc_cheat_n == 0 || g_sc_cheats_done) &&
        (g_sc_weapon < 0 || g_sc_weapon_done) &&
        (g_sc_flags == 0 || g_sc_flags_done))
        return;

    pl = g_CurrentPlayer;
    if (pl == NULL || pl->prop == NULL)
        return;

    /* Stage flags: counted from first person like the weapon, so the level's
     * own lists have started before the bits they wait for appear. */
    if (g_sc_flags != 0 && !g_sc_flags_done && g_CameraMode == CAMERAMODE_FP) {
        if (g_sc_flags_fp_frames++ >= g_sc_flags_at) {
            s32 before = objectiveregisters1;
            chrSetStageFlags(pl->prop->chr, (s32) g_sc_flags);
            g_sc_flags_done = 1;
            fprintf(stderr, "sl_cheat: stage flags %08x raised (register %08x -> %08x)\n",
                    (unsigned) g_sc_flags, (unsigned) before, (unsigned) objectiveregisters1);
        }
    }

    /* Cheats: the menu's leftover state, written as soon as there is a level
     * to apply it to. lv.c applies them from the next ticking frame. */
    if (g_sc_cheat_n != 0 && !g_sc_cheats_done) {
        int i;
        for (i = 0; i < g_sc_cheat_n; i++)
            g_CheatActivated[g_sc_cheats[i]] = 1;
        update_menu15_cheat();
        g_sc_cheats_done = 1;
        fprintf(stderr, "sl_cheat: activated");
        for (i = 0; i < g_sc_cheat_n; i++)
            fprintf(stderr, " cheat %d", g_sc_cheats[i]);
        fprintf(stderr, "  (append-sp=%d)\n", (int) g_AppendCheatSinglePlayer);
    }

    /* Weapon: after the first-person transition has equipped the level's own
     * starting weapons, and a few frames past it so that equip has settled. */
    if (g_sc_weapon >= 0 && !g_sc_weapon_done) {
        if (g_CameraMode != CAMERAMODE_FP) {
            g_sc_fp_frames = 0;
            return;
        }
        if (g_sc_fp_frames < 10) {
            g_sc_fp_frames++;
            return;
        }
        if (!bondinvItemAvailable((enum ITEM_IDS) g_sc_weapon))
            bondinvAddInvItem((enum ITEM_IDS) g_sc_weapon);
        if (g_sc_weapon != ITEM_UNARMED &&
            get_ammo_count_for_weapon((enum ITEM_IDS) g_sc_weapon) <= 0)
            add_ammo_to_weapon((enum ITEM_IDS) g_sc_weapon,
                               get_max_ammo_for_weapon((enum ITEM_IDS) g_sc_weapon));
        gunRequestHandWeaponChange(GUNLEFT, ITEM_UNARMED, 1);
        gunRequestHandWeaponChange(GUNRIGHT, g_sc_weapon, 1);
        g_sc_weapon_done = 1;
        fprintf(stderr, "sl_cheat: weapon %d requested for the right hand"
                        " (was %d, ammo %d)\n",
                g_sc_weapon, (int) getCurrentPlayerWeaponId(GUNRIGHT),
                (int) get_ammo_count_for_weapon((enum ITEM_IDS) g_sc_weapon));
    }
}

#endif /* __sgi */
