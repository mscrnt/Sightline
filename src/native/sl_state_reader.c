/**
 * Native state reader (the in-process half of trace-verify --backend native).
 *
 * Mirrors tools/trace/sltrace/state.py schema v12 exactly: same fields, same
 * big-endian serialization, same SHA-1 truncations, same liveness and
 * plausibility rules - so a native tick hashes byte-identically to an
 * emulator tick when the simulations agree.  Field access is symbolic
 * (compiled against the game's structs), so MIPS-vs-native layout drift
 * cannot skew an offset: the VALUES are what get hashed.
 *
 * The two halves are ONE contract.  A field added to this file and not to
 * state.py (or the reverse) does not fail loudly - it silently reds out every
 * comparison, which reads like a divergence and costs a day to trace back to
 * the schema.  Change both, in the same order, in the same commit.
 *
 * v11 removed the last pointers from the hash: weapon_r/weapon_l carried raw
 * PropRecord addresses, which can never agree across backends and meant every
 * armed guard mismatched by construction.  They now hash weapon IDENTITY.
 */
#ifndef __sgi

#include <ultra64.h>
#include <bondgame.h>
#include "bondview.h"
#include "player.h"

/* shim side */
extern void sl_sha1(const void *data, unsigned n, unsigned char out[20]);
extern void sl_trace_tick(u32 tick, u32 fc, const unsigned char *composite16,
                          const unsigned short *keys, const unsigned char *hashes8,
                          int nents);

extern struct player *g_CurrentPlayer;
extern ChrRecord *g_ChrSlots;
extern s32 g_NumChrSlots;
extern PropRecord *g_ActivePropsTail;
extern s32 currentFrameCounter;
extern u64 g_randomSeed;
extern s32 alarm_timer;
extern s32 objective_count;
extern s32 objectiveregisters1;
/* v12. Declared exactly as objective_status.h and front.h declare them, so a
 * type drift is a compile error rather than a silently different hash. Slot
 * counts come from the ELF (objectiveStatuses 0x28 bytes, g_CheatActivated
 * 0x50), not from counting enum entries by hand. */
extern OBJECTIVESTATUS objectiveStatuses[];
extern u8 g_CheatActivated[];

#define SL_OBJECTIVE_SLOTS 10
#define SL_CHEAT_SLOTS     80
/* v13. shot_count[7]: total, head, body, limb, gun, hat, object.
 * player.h both defines struct player_data and declares g_playerPerm; a bare
 * extern here forward-declares an INCOMPLETE type, and dereferencing it is a
 * compile error rather than anything subtle. */
#define SL_SHOT_REGISTERS  7

#define SL_MAX_ENTS 300
#define SL_MAX_PROPS 2048

/* ---- big-endian pack helpers -------------------------------------------- */
static unsigned char sl_blob[4096];
static unsigned sl_blob_n;

static void pk_reset(void) { sl_blob_n = 0; }
static void pk8(u32 v)  { sl_blob[sl_blob_n++] = (unsigned char) v; }
static void pk16(u32 v) { pk8(v >> 8); pk8(v); }
static void pk32(u32 v) { pk16(v >> 16); pk16(v); }
static void pk64(u64 v) { pk32((u32) (v >> 32)); pk32((u32) v); }
static void pkf(f32 v)  { union { f32 f; u32 u; } c; c.f = v; pk32(c.u); }

static int sl_finite(f32 v) { union { f32 f; u32 u; } c; c.f = v; return (c.u & 0x7F800000u) != 0x7F800000u; }
static int sl_ptr_ok(const void *p) { return p != NULL && (u32) p >= 0x10000u; }

/* ---- globals hash: pack(">QiiiI") --------------------------------------- */
static void sl_globals_hash(unsigned char out8[8])
{
    unsigned char d[20];

    int i;

    pk_reset();
    pk64(g_randomSeed);
    pk32((u32) alarm_timer);
    pk32((u32) objective_count);
    pk32((u32) objectiveregisters1);
    pk32((u32) g_NumChrSlots);
    for (i = 0; i < SL_OBJECTIVE_SLOTS; i++)
    {
        pk32((u32) objectiveStatuses[i]);
    }
    for (i = 0; i < SL_CHEAT_SLOTS; i++)
    {
        pk8((u32) g_CheatActivated[i]);
    }
    sl_sha1(sl_blob, sl_blob_n, d);
    memcpy(out8, d, 8);
}

/* ---- player hash: pack(">fffffffffiffif"), plausibility-gated ----------- */
static void sl_player_hash(unsigned char out8[8])
{
    struct player *p = g_CurrentPlayer;
    unsigned char d[20];
    f32 chk[8];
    int i;

    memset(out8, 0, 8);
    if (!sl_ptr_ok(p))
        return;

    chk[0] = p->pos.f[0]; chk[1] = p->pos.f[1]; chk[2] = p->pos.f[2];
    chk[3] = p->current_model_pos.f[0]; chk[4] = p->current_model_pos.f[1];
    chk[5] = p->current_model_pos.f[2];
    chk[6] = p->bondhealth; chk[7] = p->bondarmour;
    for (i = 0; i < 8; i++)
        if (!sl_finite(chk[i]))
            return;
    for (i = 0; i < 3; i++)
        if (chk[i] > 1e6f || chk[i] < -1e6f)
            return;
    if (!(p->bondhealth >= 0.0f && p->bondhealth <= 1e4f))
        return;
    if (!(p->bondarmour >= 0.0f && p->bondarmour <= 1e4f))
        return;
    /* v13. Health and position do NOT notice the level tearing down: measured
     * on dam, 76 ticks of 22,686 kept a valid pointer and sane position and
     * health while ammo read +/-1e9. Ammo is the field that notices. -1 is the
     * codebase's "unset" sentinel, so it is legitimate; real counts are small. */
    for (i = 0; i < 30; i++)
        if (p->ammoheldarr[i] < -1 || p->ammoheldarr[i] > 10000)
            return;
    for (i = 0; i < 2; i++)
        if (p->hands[i].weapon_ammo_in_magazine < -1
            || p->hands[i].weapon_ammo_in_magazine > 10000)
            return;

    pk_reset();
    pkf(p->pos.f[0]); pkf(p->pos.f[1]); pkf(p->pos.f[2]);
    pkf(p->current_model_pos.f[0]); pkf(p->current_model_pos.f[1]); pkf(p->current_model_pos.f[2]);
    pkf(p->current_room_pos.f[0]); pkf(p->current_room_pos.f[1]); pkf(p->current_room_pos.f[2]);
    pk32((u32) p->crouchpos);
    pkf(p->vertical_bounce_adjust);
    pkf(p->bondhealth);
    pk32((u32) p->bonddead);
    pkf(p->bondarmour);
    /* v13. Ammo moves on every shot and pickup, which makes it one of the
     * sharpest divergence detectors available.  shot_count separates "the AI
     * got shot" from "shot somewhere that mattered": HIT_GUN and HIT_HAT
     * register a hit and do zero damage, so a health delta cannot see them. */
    for (i = 0; i < 30; i++)
    {
        pk32((u32) p->ammoheldarr[i]);
    }
    for (i = 0; i < 2; i++)
    {
        pk32((u32) p->hands[i].weaponnum);
        pk32((u32) p->hands[i].weapon_ammo_in_magazine);
    }
    for (i = 0; i < SL_SHOT_REGISTERS; i++)
    {
        pk32((u32) (sl_ptr_ok(g_playerPerm) ? g_playerPerm->shot_count[i] : 0));
    }
    sl_sha1(sl_blob, sl_blob_n, d);
    memcpy(out8, d, 8);
}

/* A held weapon's stable identity - the ObjectRecord's setup id (a PROP_*
 * value), which is the same number on every backend.  v10 hashed the raw
 * PropRecord pointer instead, so every armed guard mismatched by construction.
 * Absent slot is 0xFFFFFFFF.  The value legitimately changes mid-run when a
 * guard drops what it holds. */
static u32 sl_weapon_identity(PropRecord *w)
{
    if (!sl_ptr_ok(w) || !sl_ptr_ok(w->obj))
        return 0xFFFFFFFFu;
    return (u32) (s32) w->obj->obj;
}

/* ---- one chr slot: _ENTITY_FIELDS order then the three coord3ds --------- */
static void sl_entity_hash(ChrRecord *c, unsigned char out8[8])
{
    unsigned char d[20];
    Model *m = c->model;
    f32 af1 = 0.0f, af2 = 0.0f, sc = 0.0f;

    if (sl_ptr_ok(m)) {
        af1 = m->animframe1;
        af2 = m->animframe2;
        sc = m->scale;
    }

    pk_reset();
    pk16((u32) (u16) c->chrnum);
    pk32(c->chrflags);
    pkf(c->damage);
    pkf(c->maxdamage);
    pk32((u32) c->timer60);
    pk32(sl_weapon_identity(c->weapons_held[0]));   /* v11: identity, not address */
    pk32(sl_weapon_identity(c->weapons_held[1]));
    pkf(af1); pkf(af2); pkf(sc);
    pk8((u32) c->actiontype);
    pk8((u32) (u8) c->accuracyrating);
    pk8((u32) (u8) c->speedrating);
    pk8(c->firecount[0]);
    pk8(c->firecount[1]);
    pk8((u32) (u8) c->sleep);
    pk8((u32) (u8) c->invalidmove);
    pk8((u32) (u8) c->numclosearghs);
    pk8((u32) (u8) c->numarghs);
    pk8((u32) (u8) c->arghrating);
    pk8((u32) (u8) c->aimendcount);
    pk8(c->grenadeprob);
    pk8((u32) (u8) c->flinchcnt);
    pk16(c->hidden);
    pk32((u32) c->lastwalk60);
    pk32((u32) c->lastmoveok60);
    pkf(c->visionrange);
    pk32((u32) c->lastseetarget60);
    pk16((u32) (u16) c->lastshooter);
    pk16((u32) (u16) c->timeshooter);
    pkf(c->hearingscale);
    pk32((u32) c->lastheartarget60);
    pk16(c->aioffset);
    pk16((u32) (u16) c->aireturnlist);
    pk16((u32) (u16) c->chrseeshot);
    pk16((u32) (u16) c->chrseedie);
    pkf(c->prevpos.f[0]); pkf(c->prevpos.f[1]); pkf(c->prevpos.f[2]);
    pkf(c->fallspeed.f[0]); pkf(c->fallspeed.f[1]); pkf(c->fallspeed.f[2]);
    pkf(c->lastknowntargetpos.f[0]); pkf(c->lastknowntargetpos.f[1]);
    pkf(c->lastknowntargetpos.f[2]);
    sl_sha1(sl_blob, sl_blob_n, d);
    memcpy(out8, d, 8);
}

/* ---- world props digest: tail-to-head, exactly the schema's walk -------- */
extern void sl_sha1_stream_begin(void);
extern void sl_sha1_stream_update(const void *data, unsigned n);
extern void sl_sha1_stream_end(unsigned char out20[20]);

static PropRecord *sl_prop_seen[SL_MAX_PROPS];

static int sl_prop_seen_add(PropRecord *p, int n)
{
    int i;
    for (i = 0; i < n; i++)
        if (sl_prop_seen[i] == p)
            return -1;
    sl_prop_seen[n] = p;
    return 0;
}

extern long sl_env_s32(const char *name, long dflt);
extern int sl_detail_active;
extern void sl_detail_log(const char *fmt, ...);

static void sl_stream_update_logged(const void *data, unsigned n)
{
    sl_sha1_stream_update(data, n);
    if (sl_detail_active) {
        const unsigned char *b = data;
        unsigned i;
        char line[600];
        char *w = line;
        for (i = 0; i < n && i < 64; i++)
            w += 2, w[-2] = "0123456789abcdef"[b[i] >> 4], w[-1] = "0123456789abcdef"[b[i] & 15];
        *w = 0;
        sl_detail_log("blob %s\n", line);
    }
}

static void sl_props_hash(unsigned char out8[8])
{
    unsigned char d[20];
    PropRecord *p = g_ActivePropsTail;
    int count = 0, nseen = 0;

    sl_sha1_stream_begin();
    while (p != NULL && sl_ptr_ok(p) && count < SL_MAX_PROPS) {
        if (sl_prop_seen_add(p, nseen) < 0)
            break;
        nseen++;
        if (sl_detail_active)
            sl_detail_log("prop type=%d flags=%02x ttr=%d pos=%08x,%08x,%08x\n",
                          p->type, p->flags, p->timetoregen,
                          *(u32 *) &p->pos.f[0], *(u32 *) &p->pos.f[1],
                          *(u32 *) &p->pos.f[2]);
        if (p->type == 1 || p->type == 2 || p->type == 4
            || p->type == 7 || p->type == 8) {
            count++;
            pk_reset();
            pk8(p->type);
            pk8(p->flags);
            pk16((u32) (u16) p->timetoregen);
            pkf(p->pos.f[0]); pkf(p->pos.f[1]); pkf(p->pos.f[2]);
            sl_stream_update_logged(sl_blob, sl_blob_n);
            if ((p->type == 1 || p->type == 2 || p->type == 4)
                && sl_ptr_ok(p->obj)) {
                ObjectRecord *o = p->obj;
                if (sl_detail_active)
                    sl_detail_log("  obj id=%d state=%02x flags=%08x flags2=%08x\n",
                                  o->obj, o->state, o->flags, o->flags2);
                pk_reset();
                pk16((u32) (u16) o->obj);
                pk8(o->state);
                pk32(o->flags);
                pk32(o->flags2);
                sl_stream_update_logged(sl_blob, sl_blob_n);
                if (p->type == 2) {
                    DoorRecord *dr = (DoorRecord *) o;
                    if (sl_detail_active)
                        sl_detail_log("  door frac=%08x open=%d\n", *(u32 *) &dr->unkac, dr->openstate);
                    pk_reset();
                    /* the schema's DOOR_FRAC_OFF is MIPS offset 0xAC, which
                     * is unkac - frac sits at 0xA8.  Hash what the emulator
                     * hashes. */
                    pkf(dr->unkac);
                    pk8((u32) (u8) dr->openstate);
                    sl_stream_update_logged(sl_blob, sl_blob_n);
                } else if (p->type == 4) {
                    WeaponObjRecord *w = (WeaponObjRecord *) o;
                    pk_reset();
                    pk8((u32) (u8) w->weaponnum);
                    pk16((u32) (u16) w->timer);
                    sl_stream_update_logged(sl_blob, sl_blob_n);
                } else if (p->type == 1) {
                    /* v13. Cameras and turrets were already hashed as objects
                     * - existence, destroyed and activated bits.  What was
                     * missing is where they POINT, and the CCTV detection
                     * timer, which advances only inside a +/-45 degree cone
                     * with an unobstructed stan line and so drifts long
                     * before the alarm outcome changes. */
                    if (o->type == PROPDEF_CCTV) {
                        struct CCTVRecord *c = (struct CCTVRecord *) o;
                        pk_reset();
                        pkf(c->unkC8);
                        pk32((u32) c->timer);
                        sl_stream_update_logged(sl_blob, sl_blob_n);
                    } else if (o->type == PROPDEF_AUTOGUN) {
                        AutogunRecord *a = (AutogunRecord *) o;
                        pk_reset();
                        pkf(a->rot_related);
                        pk32((u32) a->is_active);
                        sl_stream_update_logged(sl_blob, sl_blob_n);
                    }
                }
            }
        }
        p = p->prev;
    }
    pk_reset();
    pk16((u32) count);
    sl_stream_update_logged(sl_blob, sl_blob_n);
    sl_sha1_stream_end(d);
    memcpy(out8, d, 8);
}

/* ---- per-tick capture ---------------------------------------------------
 * Key layout matches the schema: chrnum & 0xFFFF, collisions and 0xFFFF
 * pushed to 0x8000|slot; player at 0xFFFF; props digest at 0xFFFE.
 * Keys are emitted sorted, matching the composite accumulation order.
 */
void sl_state_capture(u32 tick)
{
    static unsigned short keys[SL_MAX_ENTS];
    static unsigned char hashes[SL_MAX_ENTS * 8];
    unsigned char gh[8], ph[8], prh[8], comp[20];
    unsigned char final16[16];
    int n = 0, i, j;
    s32 slot;

    sl_detail_active = (sl_env_s32("SL_TRACE_DETAIL", -1) == (long) tick);
    /* SL_TRACE_DETAIL=-4: the controller sample the game consumed this frame.
     * g_ContData (src/joy.c) has no header; the layout is samples[20] of 24
     * bytes (pads[4] of 6), then curlast at 0x1E0 and curstart at 0x1E4. */
    if (sl_env_s32("SL_TRACE_DETAIL", -1) == -4) {
        extern unsigned char g_ContData[];
        int curlast = *(int *) (g_ContData + 0x1E0);
        unsigned char *smp = g_ContData + (curlast % 20) * 24;
        sl_detail_log("C %u %d %04x %d %d\n", tick, currentFrameCounter,
                      *(unsigned short *) smp, (int) (signed char) smp[2],
                      (int) (signed char) smp[3]);
    }
    /* SL_TRACE_DETAIL=-2: one compact player-position line per tick, as raw
     * float bits - the ULP-drift growth curve against the emulator */
    if (sl_env_s32("SL_TRACE_DETAIL", -1) == -2 && g_CurrentPlayer != NULL) {
        struct player *pl = g_CurrentPlayer;
        sl_detail_log("P %u %d %08x %08x %08x\n", tick, currentFrameCounter,
                      *(u32 *) &pl->pos.f[0], *(u32 *) &pl->pos.f[1],
                      *(u32 *) &pl->pos.f[2]);
    }
    if (sl_detail_active) {
        struct player *pl = g_CurrentPlayer;
        sl_detail_log("== native tick %u fc=%d ==\n", tick, currentFrameCounter);
        if (pl != NULL)
            sl_detail_log("player pos=%f,%f,%f model=%f,%f,%f room=%f,%f,%f crouch=%d bounce=%f health=%f dead=%d armour=%f\n",
                          pl->pos.f[0], pl->pos.f[1], pl->pos.f[2],
                          pl->current_model_pos.f[0], pl->current_model_pos.f[1], pl->current_model_pos.f[2],
                          pl->current_room_pos.f[0], pl->current_room_pos.f[1], pl->current_room_pos.f[2],
                          pl->crouchpos, pl->vertical_bounce_adjust,
                          pl->bondhealth, pl->bonddead, pl->bondarmour);
    }

    sl_globals_hash(gh);
    sl_player_hash(ph);

    if (sl_ptr_ok(g_ChrSlots) && g_NumChrSlots > 0 && g_NumChrSlots <= 256) {
        for (slot = 0; slot < g_NumChrSlots && n < SL_MAX_ENTS - 2; slot++) {
            ChrRecord *c = &g_ChrSlots[slot];
            unsigned key;
            if (!sl_ptr_ok(c->model))
                continue;
            key = (u16) c->chrnum;
            if (key == 0xFFFFu)
                key = 0x8000u | (slot & 0x7FFF);
            else {
                for (i = 0; i < n; i++)
                    if (keys[i] == key) {
                        key = 0x8000u | (slot & 0x7FFF);
                        break;
                    }
            }
            keys[n] = (unsigned short) key;
            sl_entity_hash(c, &hashes[n * 8]);
            n++;
        }
    }

    sl_props_hash(prh);

    /* sort keys (insertion; n is small) with their hashes */
    for (i = 1; i < n; i++) {
        unsigned short k = keys[i];
        unsigned char h[8];
        memcpy(h, &hashes[i * 8], 8);
        for (j = i - 1; j >= 0 && keys[j] > k; j--) {
            keys[j + 1] = keys[j];
            memcpy(&hashes[(j + 1) * 8], &hashes[j * 8], 8);
        }
        keys[j + 1] = k;
        memcpy(&hashes[(j + 1) * 8], h, 8);
    }

    /* composite: sha1(gh + ph + props + sorted (">H" key + hash8)) [:16] */
    sl_sha1_stream_begin();
    sl_sha1_stream_update(gh, 8);
    sl_sha1_stream_update(ph, 8);
    sl_sha1_stream_update(prh, 8);
    for (i = 0; i < n; i++) {
        unsigned char kb[2];
        kb[0] = (unsigned char) (keys[i] >> 8);
        kb[1] = (unsigned char) keys[i];
        sl_sha1_stream_update(kb, 2);
        sl_sha1_stream_update(&hashes[i * 8], 8);
    }
    sl_sha1_stream_end(comp);
    memcpy(final16, comp, 16);

    /* append player and props as addressable entities, like the schema */
    keys[n] = 0xFFFE; memcpy(&hashes[n * 8], prh, 8); n++;
    keys[n] = 0xFFFF; memcpy(&hashes[n * 8], ph, 8); n++;

    sl_trace_tick(tick, (u32) currentFrameCounter, final16, keys, hashes, n);
}

#endif /* !__sgi */
