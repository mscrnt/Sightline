/**
 * The crash report's [models] section, native side.
 *
 * WHAT THIS IS FOR. Three playtest faults (B-FRG3, B-BNK2B and the Bunker 2
 * run of 2026-10-07 12:06) died in model.c following a pointer read out of
 * model state - a held weapon's and a monitor's attachedto_objinst, a
 * character's node walk - whose top byte was wrong while its low three bytes
 * were a valid node (0x20 -> 0xff, 0x14, 0x3e). The report could name the
 * instruction and the call chain, never the Model, the field, the bytes round
 * it or the thing the Model belongs to, and none of the three reproduced; the
 * writer (an unswapped blood-stain link, src/native/sl_swap_model.c) was found
 * only by a heap-wide watch in a scratch build. This section is the evidence
 * that would have named the victim at the first fault, taken from memory that
 * still holds it:
 *
 *   model-frame   every frame of the Ebp chain with its first three argument
 *                 words; an argument that is a live Model is marked, and its
 *                 slot is dumped below;
 *   model-bad     every live Model (each character's, each object's, each
 *                 weapon's and door's) whose pointer fields fail the checks
 *                 below, with its owner, the presentation in force, whether a
 *                 presentation item rides its model file, and the bytes from
 *                 0x20 before the slot to 0x40 after its start.
 *
 * THE CHECKS. A pointer field (obj, render_pos, datas, attachedto,
 * attachedto_objinst, and every node's Parent/Next/Child in the model file) is
 * sound when it is NULL, or lies where game data lives - the game heap, the
 * image and the low statics, or the cartridge pager's window - and is
 * readable for its record. A changed top byte almost always leaves those
 * ranges (all three faults read FREE or RESERVE memory); one that lands
 * inside them and stays readable passes, and that is the check's blind spot.
 * The node walk of each distinct model file is bounded (4096 nodes, 64
 * files) and stops at a pager page not yet touched rather than fault in the
 * handler.
 *
 * COST: nothing in play. It runs only inside the fault handler, through the
 * caller's readability predicate, writes nothing, allocates nothing.
 */
#ifndef __sgi

#include <ultra64.h>
#include <bondgame.h>
#include "chr.h"
#include "chrai.h"
#include "model.h"
#include "objecthandler.h"
#include "chrobjdata.h"

extern int snprintf(char *, unsigned int, const char *, ...);

extern PropRecord g_Props[];
extern struct ModelSlot *g_ModelSlots;
extern struct AnimModelSlot *g_AnimModelSlots;
extern int sl_presentation(void);
extern const char *sl_presentation_name(int presentation);
extern unsigned sl_chr_asset_rides(const void *key);

#define SL_CM_ANIM_STRIDE 0xbc          /* g_AnimModelSlots[i]: sizeof(struct AnimModelSlot) */
#define SL_CM_MAX_BAD     6
#define SL_CM_MAX_FILES   64
#define SL_CM_MAX_NODES   4096

static int (*g_rd)(const void *, unsigned long);
static char *g_out;
static s32 g_n, g_at;

#define MCAT(...)  do {                                                       \
        int k_;                                                               \
        if (g_at >= g_n - 1) break;                                           \
        k_ = snprintf(g_out + g_at, (unsigned int) (g_n - g_at), __VA_ARGS__); \
        if (k_ > 0) g_at += k_;                                               \
        if (g_at > g_n - 1) g_at = g_n - 1;                                    \
    } while (0)

static int rd(const void *p, unsigned long n)
{
    return p != NULL && (g_rd == NULL || g_rd(p, n));
}

/* Where game data can live: the game heap's 16 MB, the image and what lies
 * below 0x10000000 (the compiled-in files), or the cartridge pager's window,
 * whose pages are readable once touched (the pager commits them on the fault). */
extern void *sl_native_heap(u32 *size);
#define SL_CM_PAGER_LO 0xC0000000ul
#define SL_CM_PAGER_HI 0xC1000000ul

static int plausible(const void *p)
{
    unsigned long a = (unsigned long) p;
    u32 sz = 0;
    unsigned long h = (unsigned long) sl_native_heap(&sz);
    return (a >= h && a < h + sz) || a < 0x10000000ul
        || (a >= SL_CM_PAGER_LO && a < SL_CM_PAGER_HI);
}

/* A pointer field is sound when it is NULL, or plausible and readable for
 * its record (a pager page counts as readable). */
static int ptr_ok(const void *p, unsigned long n)
{
    unsigned long a = (unsigned long) p;
    if (p == NULL) return 1;
    if (!plausible(p)) return 0;
    return (a >= SL_CM_PAGER_LO && a < SL_CM_PAGER_HI) || rd(p, n);
}

/* ---- where a Model lives and who owns it ------------------------------- */

/* "slot N" in the 32-byte pool, "anim N" in the animated pool, else "-". */
static void slot_of(const Model *m, char *b, int n)
{
    unsigned long a = (unsigned long) m;
    unsigned long ms = (unsigned long) g_ModelSlots, as = (unsigned long) g_AnimModelSlots;
    if (g_ModelSlots != NULL && g_MaxModelSlots > 0 && a >= ms
        && a < ms + (unsigned long) g_MaxModelSlots * sizeof(struct ModelSlot)) {
        snprintf(b, (unsigned int) n, "slot %lu%s", (a - ms) / sizeof(struct ModelSlot),
                 ((a - ms) % sizeof(struct ModelSlot)) ? " (unaligned)" : "");
        return;
    }
    if (g_AnimModelSlots != NULL && g_MaxAnimModelSlots > 0 && a >= as
        && a < as + (unsigned long) g_MaxAnimModelSlots * SL_CM_ANIM_STRIDE) {
        snprintf(b, (unsigned int) n, "anim %lu%s", (a - as) / SL_CM_ANIM_STRIDE,
                 ((a - as) % SL_CM_ANIM_STRIDE) ? " (unaligned)" : "");
        return;
    }
    snprintf(b, (unsigned int) n, "-");
}

static int prop_has_obj(const PropRecord *p)
{
    return p->type == PROP_TYPE_OBJ || p->type == PROP_TYPE_WEAPON || p->type == PROP_TYPE_DOOR;
}

/* One owner line for `m`: the character or the prop whose model it is. */
static int owner(const Model *m)
{
    int i;
    if (g_ChrSlots != NULL && g_NumChrSlots > 0 && g_NumChrSlots <= 1024
        && rd(g_ChrSlots, (unsigned long) g_NumChrSlots * sizeof(ChrRecord)))
        for (i = 0; i < g_NumChrSlots; i++) {
            const ChrRecord *c = &g_ChrSlots[i];
            if (c->model != m) continue;
            MCAT("model-owner         chr slot=%d chrnum=%d body=%d(%s) head=%d replacement=%s\n",
                 i, (int) c->chrnum, (int) c->bodynum,
                 (c->bodynum >= 0 && c_item_entries[c->bodynum].filename) ? c_item_entries[c->bodynum].filename : "-",
                 (int) c->headnum,
                 (c->bodynum >= 0 && (sl_chr_asset_rides(c_item_entries[c->bodynum].header) & 1u)) ? "char item" : "none");
            return 1;
        }
    for (i = 0; i < MAX_PROPS; i++) {
        const PropRecord *p = &g_Props[i];
        const ObjectRecord *o;
        if (!prop_has_obj(p) || !rd(p->obj, sizeof(ObjectRecord))) continue;
        o = p->obj;
        if (o->model != m) continue;
        MCAT("model-owner         prop=%d proptype=%d objtype=%d model#=%d(%s) parent=%d replacement=%s\n",
             i, (int) p->type, (int) (u8) o->type, (int) o->obj,
             (o->obj >= 0 && PitemZ_entries[o->obj].filename) ? PitemZ_entries[o->obj].filename : "-",
             p->parent != NULL ? (int) (p->parent - g_Props) : -1,
             (o->obj >= 0 && (sl_chr_asset_rides(PitemZ_entries[o->obj].header) & 2u)) ? "prop item" : "none");
        return 1;
    }
    MCAT("model-owner         none found among characters and props\n");
    return 0;
}

static void dump(const Model *m)
{
    const unsigned char *b = (const unsigned char *) m - 0x20;
    int k, j;
    for (k = 0; k < 0x60; k += 16) {
        if (!rd(b + k, 16)) { MCAT("model-bytes         %+04x: unreadable\n", k - 0x20); continue; }
        MCAT("model-bytes         %+04x:", k - 0x20);
        for (j = 0; j < 16; j++)
            MCAT("%s%02x", (j % 4) == 0 ? " " : "", b[k + j]);
        MCAT("\n");
    }
}

/* ---- the checks --------------------------------------------------------- */

static const void *g_files_ok[SL_CM_MAX_FILES];
static int g_nfiles_ok;

/* The model file's node tree. Returns NULL when sound, else what failed. */
static const char *file_bad(const ModelFileHeader *h, const ModelNode **at)
{
    const ModelNode *nd;
    int i, guard = 0;
    for (i = 0; i < g_nfiles_ok; i++) if (g_files_ok[i] == h) return NULL;
    if (!rd(h, sizeof(ModelFileHeader))) return "obj (model file) unreadable";
    nd = h->RootNode;
    if (nd == NULL) return NULL;
    if (!rd(nd, sizeof(ModelNode))) { *at = nd; return "obj->RootNode unreadable"; }
    while (nd != NULL && guard++ < SL_CM_MAX_NODES) {
        *at = nd;
        if (!rd(nd, sizeof(ModelNode))) break;            /* a pager page not yet touched */
        if (nd->Child != NULL && !ptr_ok(nd->Child, sizeof(ModelNode)))
            return "a node's Child";
        if (nd->Next != NULL && !ptr_ok(nd->Next, sizeof(ModelNode)))
            return "a node's Next";
        if (nd->Parent != NULL && !ptr_ok(nd->Parent, sizeof(ModelNode)))
            return "a node's Parent";
        if (nd->Child != NULL) { nd = nd->Child; continue; }
        while (nd != NULL && nd->Next == NULL) nd = nd->Parent;
        if (nd != NULL) nd = nd->Next;
    }
    if (g_nfiles_ok < SL_CM_MAX_FILES) g_files_ok[g_nfiles_ok++] = h;
    return NULL;
}

/* NULL when the Model's fields are sound, else what failed (and the node). */
static const char *model_bad(const Model *m, const ModelNode **at)
{
    *at = NULL;
    if (!rd(m, 0x20)) return "the slot itself unreadable";
    if (m->obj == NULL) return NULL;                      /* a free slot */
    if (!ptr_ok(m->obj, sizeof(ModelFileHeader))) return "obj";
    if (!ptr_ok(m->render_pos, 64)) return "render_pos";
    if (!ptr_ok(m->datas, 4)) return "datas";
    if (!ptr_ok(m->attachedto, 0x20)) return "attachedto";
    if (!ptr_ok(m->attachedto_objinst, sizeof(ModelNode))) {
        *at = m->attachedto_objinst;
        return "attachedto_objinst";
    }
    return file_bad(m->obj, at);
}

static int g_nbad;
static const Model *g_reported[SL_CM_MAX_BAD * 2];
static int g_nreported;

static void report(const Model *m, const char *why, const ModelNode *at, const char *how)
{
    char slot[48];
    int i;
    for (i = 0; i < g_nreported; i++) if (g_reported[i] == m) return;
    if (g_nreported < (int) (sizeof g_reported / sizeof g_reported[0])) g_reported[g_nreported++] = m;
    slot_of(m, slot, (int) sizeof slot);
    MCAT("model-bad           %s model=%p %s (%s) node=%p presentation=%s\n", how, (const void *) m,
         slot, why, (const void *) at, sl_presentation_name(sl_presentation()));
    if (rd(m, 0x20))
        MCAT("model-fields        obj=%p render_pos=%p datas=%p attachedto=%p attachedto_objinst=%p\n",
             (void *) m->obj, (void *) m->render_pos, (void *) m->datas,
             (void *) m->attachedto, (void *) m->attachedto_objinst);
    owner(m);
    dump(m);
}

static void census_one(const Model *m, const char *how)
{
    const ModelNode *at;
    const char *why;
    if (m == NULL || g_nbad >= SL_CM_MAX_BAD) return;
    why = model_bad(m, &at);
    if (why == NULL) return;
    g_nbad++;
    report(m, why, at, how);
}

/* Is `p` a live Model the census knows (a character's or a prop's)? */
static int live_model(const void *p)
{
    int i;
    if (p == NULL) return 0;
    if (g_ChrSlots != NULL && g_NumChrSlots > 0 && g_NumChrSlots <= 1024
        && rd(g_ChrSlots, (unsigned long) g_NumChrSlots * sizeof(ChrRecord)))
        for (i = 0; i < g_NumChrSlots; i++)
            if ((const void *) g_ChrSlots[i].model == p) return 1;
    for (i = 0; i < MAX_PROPS; i++)
        if (prop_has_obj(&g_Props[i]) && rd(g_Props[i].obj, sizeof(ObjectRecord))
            && (const void *) g_Props[i].obj->model == p)
            return 1;
    return 0;
}

s32 sl_crash_model_scan(char *out, s32 n, int (*readable)(const void *, unsigned long),
                        unsigned long bp)
{
    int i, k;
    if (out == NULL || n < 64) return 0;
    out[0] = '\0';
    g_out = out; g_n = n; g_at = 0; g_rd = readable;
    g_nbad = 0; g_nreported = 0; g_nfiles_ok = 0;

    MCAT("\n[models]\nmodel-presentation  %s\n", sl_presentation_name(sl_presentation()));

    /* the frames, and any Model handed to them */
    for (i = 0; i < 10 && bp > 0x1000 && bp < 0xffffe000ul; i++) {
        const unsigned long *f = (const unsigned long *) bp;
        if (!rd(f, 5 * sizeof *f)) break;
        MCAT("model-frame         %d ra=%08lx args %08lx %08lx %08lx", i, f[1], f[2], f[3], f[4]);
        for (k = 2; k < 5; k++)
            if (live_model((const void *) f[k])) MCAT(" [arg%d is a live Model]", k - 2);
        MCAT("\n");
        for (k = 2; k < 5; k++)
            if (live_model((const void *) f[k])) {
                const ModelNode *at;
                const char *why = model_bad((const Model *) f[k], &at);
                report((const Model *) f[k], why != NULL ? why : "fields sound", at, "frame-arg");
            }
        if (f[0] <= bp) break;
        bp = f[0];
    }

    /* every live Model */
    if (g_ChrSlots != NULL && g_NumChrSlots > 0 && g_NumChrSlots <= 1024
        && rd(g_ChrSlots, (unsigned long) g_NumChrSlots * sizeof(ChrRecord)))
        for (i = 0; i < g_NumChrSlots; i++)
            census_one(g_ChrSlots[i].model, "chr");
    for (i = 0; i < MAX_PROPS; i++)
        if (prop_has_obj(&g_Props[i]) && rd(g_Props[i].obj, sizeof(ObjectRecord)))
            census_one(g_Props[i].obj->model, "prop");
    MCAT("model-bad-count     %d\n", g_nbad);
    return g_at;
}

#undef MCAT

#endif /* __sgi */
