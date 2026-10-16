/**
 * Character-record pointer witness, native side. Inert unless SL_CHR_WATCH
 * is set; plus the crash report's [characters] section, which is always on.
 *
 * WHAT THIS IS FOR. The Aztec fault of 2026-09-17 (playtest run
 * 20260917-090450-lvl28) was chrlvUpdateAimendsideback (chraction.c:5968)
 * reading weapons_held[hand]->flags through 0x3333fb4c - a value that is
 * neither NULL nor an element of g_Props (chrprop.c:59, the only pool a
 * weapons_held entry is ever assigned from: propobj.c:12128 stores
 * wep->prop, which objInit took from chrpropAllocate). The crash report
 * named the consumer and the player's pose; it could not name the record
 * that held the value (a local of propsTick, three frames up, and the report
 * has no register file), the frame the field went wrong, or what else in the
 * record changed with it. Two witnesses, one file:
 *
 *   SL_CHR_WATCH=1   every frame, every live slot of g_ChrSlots is checked;
 *                    the first record whose prop-pointer fields (weapons_held,
 *                    the hat, prop) leave the pool prints
 *                      sl_chrw: f<frame> BAD slot=<i> rec=<addr> chrnum=<n>
 *                               act=<a> field=<name> value=<hex> ...
 *                    then one "diff" line per byte range that differs from
 *                    the previous frame's copy of the record, then a hex dump
 *                    of 0x140..0x180 (the aim fields, weapons_held, the
 *                    sound-state pairs) - once per slot, then silence for
 *                    that slot until it is re-initialised.
 *   SL_CHR_WATCH=2   additionally, every ACT_ATTACK entry: slot, chrnum,
 *                    both weapons_held entries, the action's attack type and
 *                    item - the action the fault was consumed from.
 *   SL_CHR_WATCH_SCAN=<hex>   every 300 frames, where that exact 32-bit
 *                    value sits in the game heap (0x20000000..0x21000000):
 *                    a producer hunt for a value that is not a pointer.
 *
 * The [characters] section of crash.txt (sl_crash_chr_scan, called by the
 * platform reporter after [teleport]) is the same pool test run once, at the
 * fault, over every live record: the corrupted record is still in memory
 * with the value in it, so the next fault of this class names its slot,
 * chrnum, action and the bytes around the field without a reproduction.
 *
 * Exercised 2026-09-17 with a scratch injection (weapons_held[0] of slot 13
 * set to 0x3333fb4c at frame 401, injection since removed): the frame
 * witness printed BAD slot=13 with the diff naming 0x160..0x163 old=dc283401
 * new=4cfb3333; the report that followed carried chr-bad slot=13 ... held=
 * 3333FB4C,00000000 and chr-bad-count 1.
 *
 * READ-ONLY, like sl_mission_dbg.c: nothing is written into the game. The
 * frame witness costs one memcpy per live record per frame only while
 * SL_CHR_WATCH is set; unset, it is one getenv on the first frame and an int
 * test thereafter.
 */
#ifndef __sgi

#include <ultra64.h>
#include <bondgame.h>
#include "chr.h"
#include "chrai.h"
#include "bondview.h"
#include "player.h"
#include "gun.h"
#include "chrobjdata.h"

extern int    fprintf(void *, const char *, ...);
extern void  *stderr;
extern char  *getenv(const char *);
extern void  *memcpy(void *, const void *, unsigned);

#define SL_CW_MAX_SLOTS 256

static int g_cw_checked;
static int g_cw_level;
static int g_cw_reported[SL_CW_MAX_SLOTS];
static unsigned char g_cw_prev_act[SL_CW_MAX_SLOTS];
static unsigned char g_cw_shadow[SL_CW_MAX_SLOTS][sizeof(ChrRecord)];
static int g_cw_have_shadow[SL_CW_MAX_SLOTS];
static int g_cw_slots_seen;

/* A prop pointer is valid when it is NULL or exactly an element of g_Props. */
static int sl_cw_prop_ok(const PropRecord *p)
{
    unsigned long a = (unsigned long) p;
    unsigned long lo = (unsigned long) &g_Props[0];
    unsigned long hi = (unsigned long) &g_Props[MAX_PROPS];
    if (p == NULL)
        return 1;
    if (a < lo || a >= hi)
        return 0;
    return ((a - lo) % sizeof(PropRecord)) == 0;
}

static int sl_cw_record_ok(const ChrRecord *c)
{
    return sl_cw_prop_ok(c->weapons_held[0]) && sl_cw_prop_ok(c->weapons_held[1])
        && sl_cw_prop_ok(c->handle_positiondata_hat)
        && sl_cw_prop_ok(c->prop) && c->prop != NULL;
}

static void sl_cw_dump_diff(int slot, const ChrRecord *c)
{
    const unsigned char *now = (const unsigned char *) c;
    const unsigned char *old = g_cw_shadow[slot];
    unsigned i = 0;

    if (!g_cw_have_shadow[slot]) {
        fprintf(stderr, "sl_chrw:   (no previous-frame copy of this slot)\n");
        return;
    }
    while (i < sizeof(ChrRecord)) {
        if (now[i] != old[i]) {
            unsigned j = i, k;
            while (j < sizeof(ChrRecord) && now[j] != old[j])
                j++;
            fprintf(stderr, "sl_chrw:   diff 0x%03x..0x%03x old=", i, j - 1);
            for (k = i; k < j && k < i + 16; k++) fprintf(stderr, "%02x", old[k]);
            fprintf(stderr, " new=");
            for (k = i; k < j && k < i + 16; k++) fprintf(stderr, "%02x", now[k]);
            fprintf(stderr, "%s\n", (j - i) > 16 ? " ..." : "");
            i = j;
        } else {
            i++;
        }
    }
}

static void sl_cw_hex(const ChrRecord *c, unsigned from, unsigned to)
{
    const unsigned char *b = (const unsigned char *) c;
    unsigned i, k;
    for (i = from; i < to; i += 16) {
        fprintf(stderr, "sl_chrw:   %03x:", i);
        for (k = 0; k < 16 && i + k < to; k++)
            fprintf(stderr, "%s%02x", (k % 4) == 0 ? " " : "", b[i + k]);
        fprintf(stderr, "\n");
    }
}

static void sl_cw_scan_heap(s32 frame)
{
    extern unsigned long strtoul(const char *, char **, int);
    const char *e = getenv("SL_CHR_WATCH_SCAN");
    unsigned v;
    unsigned long a;
    int hits = 0;

    if (e == NULL || *e == '\0')
        return;
    v = (unsigned) strtoul(e, NULL, 16);
    /* the game heap is one committed reservation (sl_ultra_shim.c, "game
     * heap 16 MB at 20000000"), so every word of it is readable */
    for (a = 0x20000000ul; a + 4 <= 0x21000000ul && hits < 24; a += 4) {
        if (*(volatile unsigned *) a == v) {
            fprintf(stderr, "sl_chrw: f%d value %08x found at %08lx\n", (int) frame, v, a);
            hits++;
        }
    }
    fprintf(stderr, "sl_chrw: f%d scan for %08x: %d hit(s)\n", (int) frame, v, hits);
}

/* SL_CHR_PRES=1 - CHARACTER PRESENTATION WITNESS (#70). Read-only, like the
 * rest of this file. Every 240 pumped frames (at most 16 times) it lists the
 * live characters nearest the player - slot, chrnum, the body and head entries
 * of c_item_entries with their model file names, position, room and distance
 * - and, once per model file, that file's texture table: each entry's
 * TextureID word (after texLoad it is the decoded pointer) resolved through
 * the texture provider's identity bridge to the N64 texture number and the
 * base size the pool recorded, beside the table's own declared size. That is
 * the identity a presentation provider would be asked about for the body and
 * the head a player sees; which of those ids the frame actually paints is the
 * renderer's SL_TEX_COVERAGE / SL_TEX_HILITE, and what the provider answered
 * is SL_TEX_PROVIDER_DBG. Unset, it is one getenv on the first frame. */
static void sl_cw_pres_model(const char *what, int idx)
{
    extern int sl_texprov_id_of(const void *src, unsigned *w, unsigned *h);
    static const void *said[64];
    static int nsaid;
    const ModelFileHeader *hd;
    int i, k;

    if (idx < 0) return;
    hd = c_item_entries[idx].header;
    if (hd == NULL) return;
    for (k = 0; k < nsaid; k++) if (said[k] == (const void *) hd) return;
    if (nsaid < 64) said[nsaid++] = (const void *) hd;
    fprintf(stderr, "sl_chrpres: model %s=%d file=%s hdr=%p textures=%d loaded=%d\n",
            what, idx, c_item_entries[idx].filename ? c_item_entries[idx].filename : "?",
            (const void *) hd, (int) hd->numtextures, (int) hd->isLoaded);
    /* the rest pose: every GROUP node of THIS file (the walk stops at the
     * file's root - a head file's root hangs under a body's head node), its
     * joint, matrix ids and origin, and the origins summed up the tree */
    {
        const ModelNode *nd = hd->RootNode;
        int guard = 0;
        while (nd != NULL && guard++ < 512) {
            float base[3];
            const ModelNode *up;
            int d = 0;
            base[0] = base[1] = base[2] = 0.0f;
            for (up = nd; up != hd->RootNode && up->Parent != NULL; up = up->Parent) {
                d++;
                if ((up->Parent->Opcode & 0xff) == MODELNODE_OPCODE_GROUP && up->Parent->Data != NULL) {
                    base[0] += up->Parent->Data->Group.Origin.f[0];
                    base[1] += up->Parent->Data->Group.Origin.f[1];
                    base[2] += up->Parent->Data->Group.Origin.f[2];
                }
            }
            if ((nd->Opcode & 0xff) == MODELNODE_OPCODE_GROUP && nd->Data != NULL) {
                const struct ModelRoData_GroupRecord *g = &nd->Data->Group;
                fprintf(stderr, "sl_chrpres:   group node=%p depth=%d joint=%u mtx=%d,%d origin=(%.3f, %.3f, %.3f) model=(%.3f, %.3f, %.3f)\n",
                        (const void *) nd, d, (unsigned) g->JointID, (int) g->MatrixID0, (int) g->MatrixID1,
                        (double) g->Origin.f[0], (double) g->Origin.f[1], (double) g->Origin.f[2],
                        (double) (base[0] + g->Origin.f[0]), (double) (base[1] + g->Origin.f[1]),
                        (double) (base[2] + g->Origin.f[2]));
            } else if ((nd->Opcode & 0xff) == MODELNODE_OPCODE_HEAD) {
                fprintf(stderr, "sl_chrpres:   head node=%p depth=%d model=(%.3f, %.3f, %.3f)\n",
                        (const void *) nd, d, (double) base[0], (double) base[1], (double) base[2]);
            }
            /* never into an attached head: its nodes are another file's */
            if (nd->Child != NULL && (nd->Opcode & 0xff) != MODELNODE_OPCODE_HEAD) { nd = nd->Child; continue; }
            while (nd != NULL && nd != hd->RootNode && nd->Next == NULL) nd = nd->Parent;
            if (nd == NULL || nd == hd->RootNode) break;
            nd = nd->Next;
        }
    }
    if (hd->Textures == NULL) return;
    for (i = 0; i < hd->numtextures && i < 64; i++) {
        const ModelFileTextures *t = &hd->Textures[i];
        unsigned w = 0, h = 0;
        int id = sl_texprov_id_of((const void *) (unsigned long) t->TextureID, &w, &h);
        fprintf(stderr, "sl_chrpres:   tex[%d] word=%08x id=%04x pool=%ux%u table=%ux%u type=%u depth=%u s=%u t=%u mips=%u\n",
                i, (unsigned) t->TextureID, id >= 0 ? (unsigned) id : 0xFFFFu, w, h,
                (unsigned) t->Width, (unsigned) t->Height, (unsigned) t->Type,
                (unsigned) t->RenderDepth, (unsigned) t->sflags, (unsigned) t->tflags,
                (unsigned) t->MipMapTiles);
    }
}

static void sl_cw_pres_frame(s32 frame)
{
    extern double sqrt(double);
    static int checked, on, dumps;
    int i;
    float px = 0.0f, py = 0.0f, pz = 0.0f;

    if (!checked) {
        const char *e = getenv("SL_CHR_PRES");
        on = (e != NULL && *e != '\0' && *e != '0');
        checked = 1;
    }
    if (!on || dumps >= 16 || (frame % 240) != 1) return;
    if (g_ChrSlots == NULL || g_NumChrSlots <= 0 || g_CurrentPlayer == NULL
        || g_CurrentPlayer->prop == NULL)
        return;
    dumps++;
    px = g_CurrentPlayer->prop->pos.f[0];
    py = g_CurrentPlayer->prop->pos.f[1];
    pz = g_CurrentPlayer->prop->pos.f[2];
    fprintf(stderr, "sl_chrpres: f%d player (%.3f, %.3f, %.3f) room %d slots=%d\n",
            (int) frame, (double) px, (double) py, (double) pz,
            (int) g_CurrentPlayer->prop->rooms[0], (int) g_NumChrSlots);
    if (dumps == 1) {
        /* the stage's whole loaded character set, once: every body and head
         * model file the setup has loaded, whether or not it is near */
        for (i = 0; c_item_entries[i].filename != NULL && i < 256; i++)
            if (c_item_entries[i].header != NULL && c_item_entries[i].header->isLoaded)
                sl_cw_pres_model("entry", i);
    }
    for (i = 0; i < g_NumChrSlots; i++) {
        const ChrRecord *c = &g_ChrSlots[i];
        const PropRecord *p = c->prop;
        double dx, dy, dz, d;
        int body, head;
        if (c->model == NULL || p == NULL) continue;
        dx = p->pos.f[0] - px; dy = p->pos.f[1] - py; dz = p->pos.f[2] - pz;
        d = sqrt(dx * dx + dy * dy + dz * dz);
        if (d > 3000.0) continue;
        body = (int) c->bodynum; head = (int) c->headnum;
        fprintf(stderr, "sl_chrpres: f%d chr slot=%d chrnum=%d body=%d(%s) head=%d(%s) pos=(%.3f, %.3f, %.3f) room=%d dist=%.1f act=%d model=%p\n",
                (int) frame, i, (int) c->chrnum,
                body, (body >= 0 && c_item_entries[body].filename) ? c_item_entries[body].filename : "-",
                head, (head >= 0 && c_item_entries[head].filename) ? c_item_entries[head].filename : "-",
                (double) p->pos.f[0], (double) p->pos.f[1], (double) p->pos.f[2],
                (int) p->rooms[0], d, (int) c->actiontype, (const void *) c->model);
        sl_cw_pres_model("body", body);
        if (head >= 0 && !c_item_entries[body].hasHead) sl_cw_pres_model("head", head);
    }
}

void sl_chr_watch_frame(s32 frame)
{
    int i, n;

    sl_cw_pres_frame(frame);
    if (!g_cw_checked) {
        const char *e = getenv("SL_CHR_WATCH");
        g_cw_level = (e != NULL && *e != '\0' && *e != '0') ? (*e - '0') : 0;
        g_cw_checked = 1;
        if (g_cw_level)
            fprintf(stderr, "sl_chrw: armed, sizeof(ChrRecord)=0x%x sizeof(PropRecord)=0x%x g_Props=%p..%p\n",
                    (unsigned) sizeof(ChrRecord), (unsigned) sizeof(PropRecord),
                    (void *) &g_Props[0], (void *) &g_Props[MAX_PROPS]);
    }
    if (!g_cw_level)
        return;
    /* the pumped-frame counter advances by two per call here, so an odd test */
    if ((frame % 300) == 299)
        sl_cw_scan_heap(frame);
    if (g_ChrSlots == NULL || g_NumChrSlots <= 0)
        return;
    n = g_NumChrSlots > SL_CW_MAX_SLOTS ? SL_CW_MAX_SLOTS : g_NumChrSlots;
    if (n != g_cw_slots_seen) {
        fprintf(stderr, "sl_chrw: f%d g_ChrSlots=%p n=%d (record 0x%x bytes, pool end %p)\n",
                (int) frame, (void *) g_ChrSlots, g_NumChrSlots,
                (unsigned) sizeof(ChrRecord), (void *) &g_ChrSlots[g_NumChrSlots]);
        if (g_CurrentPlayer != NULL)
            fprintf(stderr, "sl_chrw: f%d hand buffers right=%p left=%p size=0x%x,0x%x\n",
                    (int) frame, (void *) g_CurrentPlayer->ptr_hand_weapon_buffer[0],
                    (void *) g_CurrentPlayer->ptr_hand_weapon_buffer[1],
                    (unsigned) size_item_buffer[0], (unsigned) size_item_buffer[1]);
        g_cw_slots_seen = n;
        for (i = 0; i < SL_CW_MAX_SLOTS; i++) { g_cw_reported[i] = 0; g_cw_have_shadow[i] = 0; g_cw_prev_act[i] = 0; }
    }
    for (i = 0; i < n; i++) {
        const ChrRecord *c = &g_ChrSlots[i];
        const char *bad = NULL;
        const void *badv = NULL;

        if (c->model == NULL) {
            g_cw_reported[i] = 0;
            g_cw_have_shadow[i] = 0;
            continue;
        }
        if (g_cw_level >= 2 && c->actiontype == ACT_ATTACK && g_cw_prev_act[i] != ACT_ATTACK) {
            fprintf(stderr, "sl_chrw: f%d ATTACK slot=%d chrnum=%d held=%p,%p attacktype=0x%x item=%d\n",
                    (int) frame, i, (int) c->chrnum,
                    (void *) c->weapons_held[0], (void *) c->weapons_held[1],
                    (unsigned) c->act_attack.attacktype, (int) c->act_attack.attack_item);
        }
        g_cw_prev_act[i] = (unsigned char) c->actiontype;

        if (!sl_cw_prop_ok(c->weapons_held[0]))      { bad = "weapons_held[0]"; badv = c->weapons_held[0]; }
        else if (!sl_cw_prop_ok(c->weapons_held[1])) { bad = "weapons_held[1]"; badv = c->weapons_held[1]; }
        else if (!sl_cw_prop_ok(c->handle_positiondata_hat)) { bad = "hat"; badv = c->handle_positiondata_hat; }
        else if (!sl_cw_prop_ok(c->prop) || c->prop == NULL) { bad = "prop"; badv = c->prop; }

        if (bad != NULL && !g_cw_reported[i]) {
            g_cw_reported[i] = 1;
            fprintf(stderr, "sl_chrw: f%d BAD slot=%d rec=%p chrnum=%d act=%d field=%s value=%p held=%p,%p prop=%p model=%p ailist=%p aioffset=%u\n",
                    (int) frame, i, (const void *) c, (int) c->chrnum, (int) c->actiontype, bad, badv,
                    (void *) c->weapons_held[0], (void *) c->weapons_held[1],
                    (void *) c->prop, (void *) c->model, (void *) c->ailist, (unsigned) c->aioffset);
            sl_cw_dump_diff(i, c);
            sl_cw_hex(c, 0x140, 0x180);
        }
        memcpy(g_cw_shadow[i], c, sizeof(ChrRecord));
        g_cw_have_shadow[i] = 1;
    }
}

/* THE CRASH REPORT'S CHARACTER SCAN (see the header). Called from inside the
 * fault handler: every pointer is checked through the caller's readability
 * predicate, nothing is written, nothing is allocated. */
s32 sl_crash_chr_scan(char *out, s32 n, int (*readable)(const void *, unsigned long))
{
    extern int snprintf(char *, unsigned int, const char *, ...);
    s32 at = 0, i, cnt, shown = 0;

    if (out == NULL || n < 64) return 0;
    out[0] = '\0';
#define CCAT(...)  do {                                                       \
        int k_;                                                               \
        if (at >= n - 1) break;                                               \
        k_ = snprintf(out + at, (unsigned int) (n - at), __VA_ARGS__);         \
        if (k_ > 0) at += k_;                                                 \
        if (at > n - 1) at = n - 1;                                            \
    } while (0)
#define COK(p, len) ((p) != NULL && (readable == NULL || readable((p), (len))))

    if (g_ChrSlots == NULL || g_NumChrSlots <= 0 || g_NumChrSlots > 1024)
        return 0;
    if (!COK(g_ChrSlots, (unsigned long) g_NumChrSlots * sizeof(ChrRecord))) {
        CCAT("\n[characters]\nchr-slots           %p x %d NOT READABLE\n",
             (void *) g_ChrSlots, (int) g_NumChrSlots);
        return at;
    }
    CCAT("\n[characters]\nchr-slots           %p x %d (record 0x%x bytes)\n",
         (void *) g_ChrSlots, (int) g_NumChrSlots, (unsigned) sizeof(ChrRecord));
    for (i = 0, cnt = 0; i < g_NumChrSlots; i++) {
        const ChrRecord *c = &g_ChrSlots[i];
        const unsigned char *b = (const unsigned char *) c;
        unsigned k, j;
        if (c->model == NULL) continue;
        cnt++;
        if (sl_cw_record_ok(c))
            continue;
        if (shown++ >= 8) break;
        CCAT("chr-bad             slot=%d rec=%p chrnum=%d act=%d prop=%p model=%p held=%p,%p hat=%p ailist=%p aioffset=%u\n",
             (int) i, (const void *) c, (int) c->chrnum, (int) c->actiontype,
             (void *) c->prop, (void *) c->model, (void *) c->weapons_held[0],
             (void *) c->weapons_held[1], (void *) c->handle_positiondata_hat,
             (void *) c->ailist, (unsigned) c->aioffset);
        for (k = 0x140; k < 0x180; k += 16) {
            CCAT("chr-bytes           %03x:", k);
            for (j = 0; j < 16; j++)
                CCAT("%s%02x", (j % 4) == 0 ? " " : "", b[k + j]);
            CCAT("\n");
        }
    }
    CCAT("chr-live            %d\nchr-bad-count       %d\n", (int) cnt, (int) shown);
#undef COK
#undef CCAT
    return at;
}

#endif /* __sgi */
