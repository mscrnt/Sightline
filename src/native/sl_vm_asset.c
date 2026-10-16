/**
 * sl_vm_asset.c - the FIRST-PERSON VIEW-MODEL seam (#70), game side. See
 * src/sl_chr_asset.h (THE FIRST-PERSON VIEW MODEL) for the contract.
 *
 * WHAT THIS FILE DECIDES, per hand and frame:
 *
 *   1. whether the presentation carries a replacement for the weapon the
 *      hand's buffer holds - the provider is asked by the model file's own
 *      name (GwppksilZ -> gun/wppksil), so ORIGINAL and COMMUNITY HD, which
 *      select no world assets, ask nothing;
 *   2. whether the item rides this model - the model's positioned parts are
 *      read once per (file, item) and handed to sl_vm_derive, and a model the
 *      game dresses in the player's costume takes only an item that gates
 *      its draws on that costume (sl_chr_asset.h, THE COSTUME); a refusal is
 *      logged once and the weapon draws as the game's;
 *   3. where the item is drawn - one bridge command at the head of the view
 *      model's geometry, carrying the model's matrices and the visibility of
 *      every switch of its Switches[] table this frame - and which of the
 *      game's own lists are withheld: every list under the model's root but
 *      those under the SW_FLASH switch (Switches[1]), the muzzle flash, which
 *      the item carries on the game's own images.
 *
 * The unit is the whole weapon: the item carries the gun and the hand that
 * holds it, as the game's file does, so there is never a replacement gun in
 * a game hand or the reverse.
 *
 * THE WATCH HAND (sl_vm_watch_begin, src/sl_chr_asset.h) rides the same
 * machinery: the left arm the watch is worn on is its own model, drawn by
 * bondview2.c bondviewRenderWatch; its item is the arm, the hand and the
 * watch as one unit, plus the source's alternative sleeves, of which the
 * costume's is drawn. A costume whose cuff no sleeve serves keeps the game's
 * whole watch hand.
 *
 * THE WEAPON ON THE WATCH (sl_vm_menu_begin, src/sl_chr_asset.h): the watch's
 * pages draw a weapon from the same G<item>Z file the hand holds, so the same
 * item stands for it there, through the same map, in the watch's own pose
 * and with the look the watch's render data gives it.
 *
 * WHAT IT DOES NOT DO: write any game field, allocate game memory, touch the
 * RNG, or change which nodes the render walk visits. The view model's
 * matrices and switches are written by the game's own view-model code
 * (gunfire.c) before this reads them; modelRenderNodeGundl asks
 * sl_chr_asset_skip (which asks this file while no character is being drawn)
 * only before it EMITS. The per-tick trace is the check that says so.
 *
 * LIFETIME. A draw record lives in a ring written as the frame's display
 * list is built and read when it is interpreted; the renderer validates
 * every record (sl_vm_draw_ok). Maps are derived once per (model file name,
 * item) - the view model's buffer is reloaded on every weapon change, so the
 * file's name, not its address, is the identity - and kept for the session.
 *
 * SL_BOND_ASSETS=0 is the control: this seam (and the player-body part of
 * the character seam) stays dark under every presentation, and Bond draws
 * exactly as the game's.
 */
#ifndef __sgi

#include <ultra64.h>
#include <bondgame.h>
#include "model.h"
#include "gun.h"
#include "bondview.h"
#include "player.h"
#include "../sl_chr_asset.h"

extern int    fprintf(void *, const char *, ...);
extern void  *stderr;
extern char  *getenv(const char *);
extern void  *memset(void *, int, unsigned);
extern int    strcmp(const char *, const char *);
extern char  *strcpy(char *, const char *);

extern int         sl_wa_active(void);
extern const void *sl_wa_lookup(unsigned asset_class, const char *path);
extern char       *get_ptr_item_text_call_line(ITEM_IDS item);

#define SL_VM_RING      16
#define SL_VM_MAPS      16
#define SL_VM_KEY_MAX   32
#define SL_VM_FLASH     1          /* Switches[1] is SW_FLASH on every gun */

static struct sl_vm_draw g_ring[SL_VM_RING];
static unsigned g_ring_at;

struct map_slot {
    char key[SL_VM_KEY_MAX];
    const void *asset;
    int ok;
    struct sl_chr_map map;
};
static struct map_slot g_maps[SL_VM_MAPS];
static int g_nmaps;

#define ST_MISS      1
#define ST_REFUSED   2
#define ST_REPLACED  3
#define ST_NOSLEEVE  4

static struct {
    int active, watch;
    const ModelNode *root, *flash;
    int hand, item, state;
    const void *record;
    unsigned kept, withheld;
} g_ctx;

static unsigned g_emitted, g_withheld;

static void emit(Model *model, const ModelFileHeader *hdr, ModelRenderData *mr,
                 const void *asset, const struct sl_chr_map *map, int alternatives);

int sl_bond_assets_on(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_BOND_ASSETS");
                  on = (v == NULL || *v != '0'); }
    return on;
}

static int pres_on(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_CHR_PRES");
                  on = (v != NULL && *v != '\0' && *v != '0'); }
    return on;
}

/* The model file's own name as an item path: `prefix` + item + "Z", lowered
 * ('G' a weapon's, 'C' the watch hand's). */
static int key_of(const char *file, char prefix, char *out)
{
    int n = 0, i;
    if (file == NULL || file[0] != prefix) return 0;
    while (file[n] != '\0' && n < 64) n++;
    if (n < 3 || n - 2 >= SL_VM_KEY_MAX || file[n - 1] != 'Z') return 0;
    for (i = 1; i < n - 1; i++) {
        char c = file[i];
        if (c >= 'A' && c <= 'Z') c = (char) (c - 'A' + 'a');
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return 0;
        out[i - 1] = c;
    }
    out[n - 2] = '\0';
    return 1;
}

/* A node's origin in MODEL space: its own and every positioning ancestor's
 * origin summed (GROUP and GROUPSIMPLE both lead with it). */
static int model_origin(const ModelNode *nd, float o[3])
{
    int guard = 0;
    o[0] = o[1] = o[2] = 0.0f;
    for (; nd != NULL && guard++ < 64; nd = nd->Parent) {
        int op = nd->Opcode & 0xff;
        if ((op == MODELNODE_OPCODE_GROUP || op == MODELNODE_OPCODE_GROUPSIMPLE) && nd->Data != NULL) {
            o[0] += nd->Data->Group.Origin.f[0];
            o[1] += nd->Data->Group.Origin.f[1];
            o[2] += nd->Data->Group.Origin.f[2];
        }
    }
    return guard < 64;
}

/* Every GROUP / GROUPSIMPLE node of the model by the matrix its geometry is
 * drawn under (a GROUP's MatrixID0, a GROUPSIMPLE's Group1), with its origin
 * in MODEL space - the parts a watch hand's joints name by matrix. */
static void gather_nodes(const ModelFileHeader *hdr, struct sl_vm_parts *p)
{
    const ModelNode *nd = hdr->RootNode;
    int guard = 0;
    while (nd != NULL && guard++ < 4096) {
        int op = nd->Opcode & 0xff, m = -1;
        if (op == MODELNODE_OPCODE_GROUP && nd->Data != NULL) m = nd->Data->Group.MatrixID0;
        else if (op == MODELNODE_OPCODE_GROUPSIMPLE && nd->Data != NULL) m = nd->Data->GroupSimple.Group1;
        if (m >= 0 && m < SL_VM_MAX_MTX && !p->node_present[m] && model_origin(nd, p->node_origin[m]))
            p->node_present[m] = 1;
        if (nd->Child != NULL) { nd = nd->Child; continue; }
        while (nd != NULL && nd != hdr->RootNode && nd->Next == NULL) nd = nd->Parent;
        if (nd == NULL || nd == hdr->RootNode) break;
        nd = nd->Next;
    }
}

static int gather(const ModelFileHeader *hdr, struct sl_vm_parts *p)
{
    const ModelNode *r = hdr->RootNode;
    int k;
    memset(p, 0, sizeof *p);
    /* a character-class model (the watch hand's) leads with a HEADER node:
     * it has no root part, and its item names its parts by matrix */
    if (r != NULL && (r->Opcode & 0xff) == MODELNODE_OPCODE_HEADER) {
        p->root_mtx = -1;
        p->mtx_count = hdr->numMatrices;
        gather_nodes(hdr, p);
        return 1;
    }
    if (r == NULL || (r->Opcode & 0xff) != MODELNODE_OPCODE_GROUP || r->Data == NULL) return 0;
    if (!model_origin(r, p->root_origin)) return 0;
    gather_nodes(hdr, p);
    p->root_mtx = r->Data->Group.MatrixID0;
    p->mtx_count = hdr->numMatrices;
    p->count = hdr->numSwitches < SL_VM_MAX_SWITCH ? hdr->numSwitches : SL_VM_MAX_SWITCH;
    for (k = 0; k < p->count; k++) {
        const ModelNode *nd = hdr->Switches[k];
        int op;
        if (nd == NULL) continue;
        op = nd->Opcode & 0xff;
        if (op != MODELNODE_OPCODE_GROUP && op != MODELNODE_OPCODE_GROUPSIMPLE) continue;
        if (!model_origin(nd, p->origin[k])) continue;
        p->mtx[k] = modelFindNodeMtxIndex((ModelNode *) nd, 0);
        if (p->mtx[k] < 0) continue;
        p->present[k] = 1;
    }
    return 1;
}

/* Does the model carry the costume cuffs (sl_chr_asset.h, THE COSTUME)? */
static int dressed(const ModelFileHeader *hdr)
{
    int k;
    for (k = SL_VM_CUFF_FIRST; k < SL_VM_CUFF_FIRST + SL_VM_CUFFS && k < hdr->numSwitches; k++)
        if (hdr->Switches[k] != NULL) return 1;
    return 0;
}

/* The box of the view model's own vertices in MODEL space - every display
 * list under the root but the muzzle flash's (sl_vm_item_static). */
static int vm_box(const ModelFileHeader *hdr, struct sl_prop_box *b)
{
    const ModelNode *nd = hdr->RootNode;
    const ModelNode *flash = (hdr->numSwitches > SL_VM_FLASH) ? hdr->Switches[SL_VM_FLASH] : NULL;
    int guard = 0, any = 0, k;
    memset(b, 0, sizeof *b);
    b->mtx_count = hdr->numMatrices;
    if (nd == NULL || (nd->Opcode & 0xff) != MODELNODE_OPCODE_GROUP || nd->Data == NULL) return 0;
    if (!model_origin(nd, b->root_origin)) return 0;
    b->root_mtx = nd->Data->Group.MatrixID0;
    while (nd != NULL && guard++ < 4096) {
        int op = nd->Opcode & 0xff;
        if (nd == flash) {
            /* the flash is the game's own: not part of the weapon's box */
        } else if (op == MODELNODE_OPCODE_DL && nd->Data != NULL
                   && nd->Data->DisplayList.Vertices != NULL) {
            float o[3];
            int i, n = nd->Data->DisplayList.numVertices;
            model_origin(nd, o);
            for (i = 0; i < n; i++)
                for (k = 0; k < 3; k++) {
                    float c = (float) nd->Data->DisplayList.Vertices[i].coord.AsArray[k] + o[k];
                    if (!any || c < b->lo[k]) b->lo[k] = c;
                    if (!any || c > b->hi[k]) b->hi[k] = c;
                    if (k == 2) any = 1;
                }
        }
        if (nd->Child != NULL && nd != flash) { nd = nd->Child; continue; }
        while (nd != NULL && nd != hdr->RootNode && nd->Next == NULL) nd = nd->Parent;
        if (nd == NULL || nd == hdr->RootNode) break;
        nd = nd->Next;
    }
    return any;
}

static const struct sl_chr_map *map_for(const ModelFileHeader *hdr, const char *key,
                                        char prefix, const void *asset)
{
    struct sl_vm_parts parts;
    char why[96];
    int i, j;
    for (i = 0; i < g_nmaps; i++)
        if (g_maps[i].asset == asset && strcmp(g_maps[i].key, key) == 0)
            return g_maps[i].ok ? &g_maps[i].map : NULL;
    if (g_nmaps >= SL_VM_MAPS) return NULL;
    i = g_nmaps++;
    strcpy(g_maps[i].key, key);
    g_maps[i].asset = asset;
    if (sl_vm_item_static(asset)) {
        struct sl_prop_box b;
        why[0] = '\0';
        g_maps[i].ok = !dressed(hdr) && vm_box(hdr, &b)
                    && sl_prop_derive(asset, &b, &g_maps[i].map, why);
        if (!g_maps[i].ok) {
            fprintf(stderr, "sl_vmasset: gun/%s REFUSED - %s; the game's weapon draws\n",
                    key, why[0] ? why : "the view model's geometry could not be read");
            return NULL;
        }
        fprintf(stderr, "sl_vmasset: gun/%s (one rigid piece) rides %c%sZ by its box - scale "
                        "%.3f, root matrix %d\n", key, prefix, key, (double) g_maps[i].map.scale,
                g_maps[i].map.mtx[0]);
        return &g_maps[i].map;
    }
    if (!gather(hdr, &parts)) {
        g_maps[i].ok = 0;
        fprintf(stderr, "sl_vmasset: gun/%s REFUSED - the view model's parts could not be "
                        "read; the game's weapon draws\n", key);
        return NULL;
    }
    g_maps[i].ok = sl_vm_derive(asset, &parts, &g_maps[i].map, why);
    if (g_maps[i].ok && dressed(hdr) && !sl_vm_item_gates(asset, SL_VM_CUFF_FIRST, SL_VM_CUFFS)) {
        g_maps[i].ok = 0;
        strcpy(why, "the model wears the player's costume (Switches[29..34]); "
                    "the item gates none of it");
    }
    if (!g_maps[i].ok) {
        fprintf(stderr, "sl_vmasset: gun/%s REFUSED - %s; the game's weapon draws\n", key, why);
        return NULL;
    }
    fprintf(stderr, "sl_vmasset: gun/%s rides %c%sZ - %d item joints, scale %.3f, matrices",
            key, prefix, key, g_maps[i].map.count, (double) g_maps[i].map.scale);
    for (j = 0; j < g_maps[i].map.count; j++)
        if (g_maps[i].map.mtx[j] >= 0 || prefix == 'G')
            fprintf(stderr, " %d:%d", j, g_maps[i].map.mtx[j]);
    if (g_maps[i].map.count > 0 && g_maps[i].map.rot[g_maps[i].map.count - 1] != 0)
        fprintf(stderr, ", %d quarter turn(s)", g_maps[i].map.rot[g_maps[i].map.count - 1]);
    fprintf(stderr, "\n");
    return &g_maps[i].map;
}

static void report(void)
{
    static const char *const nm[] = { "-", "MISS", "REFUSED", "REPLACED", "NO SLEEVE" };
    static unsigned last[2][4], lines;
    unsigned v[4];
    int h = g_ctx.hand & 1;
    v[0] = (unsigned) g_ctx.item; v[1] = (unsigned) g_ctx.state;
    v[2] = g_ctx.kept; v[3] = g_ctx.withheld;
    if (v[0] == last[h][0] && v[1] == last[h][1] && v[2] == last[h][2] && v[3] == last[h][3]) return;
    last[h][0] = v[0]; last[h][1] = v[1]; last[h][2] = v[2]; last[h][3] = v[3];
    if (lines++ >= 256) return;
    fprintf(stderr, "sl_vmpres: seam hand=%d item=%d file=%s state=%s bridge=%p "
                    "native lists kept=%u withheld=%u\n",
            g_ctx.hand, g_ctx.item,
            get_ptr_item_text_call_line((ITEM_IDS) g_ctx.item)
                ? get_ptr_item_text_call_line((ITEM_IDS) g_ctx.item) : "-",
            nm[g_ctx.state], g_ctx.record, g_ctx.kept, g_ctx.withheld);
}

void sl_vm_asset_begin(int hand, void *modelv, ModelRenderData *mr)
{
    Model *model = (Model *) modelv;
    const ModelFileHeader *hdr;
    const void *asset;
    const struct sl_chr_map *map;
    char key[SL_VM_KEY_MAX];
    int item;

    memset(&g_ctx, 0, sizeof g_ctx);
    if (!sl_bond_assets_on() || !sl_wa_active()) return;
    if (model == NULL || model->obj == NULL || mr == NULL || model->render_pos == NULL) return;
    if (hand < 0 || hand > 1 || g_CurrentPlayer == NULL) return;
    hdr = model->obj;
    item = (int) g_CurrentPlayer->hand_item[hand];
    g_ctx.watch = pres_on();
    g_ctx.hand = hand;
    g_ctx.item = item;
    g_ctx.state = ST_MISS;
    g_ctx.root = hdr->RootNode;
    if (!key_of(get_ptr_item_text_call_line((ITEM_IDS) item), 'G', key)) return;
    asset = sl_wa_lookup(SL_CHR_CLASS_GUN, key);
    if (asset == NULL) return;
    map = map_for(hdr, key, 'G', asset);
    if (map == NULL) { g_ctx.state = ST_REFUSED; return; }
    emit(model, hdr, mr, asset, map, SL_VM_ALT_ONE);
    g_ctx.flash = (hdr->numSwitches > SL_VM_FLASH) ? hdr->Switches[SL_VM_FLASH] : NULL;
}

/* One bridge record at the head of the model's geometry, carrying its
 * matrices, shade and the visibility of every switch; the model's own lists
 * are withheld from here to sl_vm_asset_end (sl_vm_asset_skip). */
static void emit(Model *model, const ModelFileHeader *hdr, ModelRenderData *mr,
                 const void *asset, const struct sl_chr_map *map, int alternatives)
{
    struct sl_vm_draw *r;
    int k;
    if (mr->flags & 1) {
        Gfx *gdl = mr->gdl;
        r = &g_ring[g_ring_at++ % SL_VM_RING];
        memset(r, 0, sizeof *r);
        r->magic = SL_VM_DRAW_MAGIC;
        r->asset = asset;
        r->map = map;
        r->mtx = (const void *) model->render_pos;
        r->mtx_count = hdr->numMatrices;
        r->alternatives = alternatives;
        r->fade = 255;
        /* the view model's shade register: gunRenderFirstPersonGunModels's
         * envcolour (the player's tile colour), which the model's render
         * mode (modelApplyRenderModeType3, PropType 4) loads as its fog */
        r->shade[0] = (unsigned char) ((mr->envcolour.word >> 24) & 0xff);
        r->shade[1] = (unsigned char) ((mr->envcolour.word >> 16) & 0xff);
        r->shade[2] = (unsigned char) ((mr->envcolour.word >> 8) & 0xff);
        r->shade[3] = (unsigned char) (mr->envcolour.word & 0xff);
        /* a switch the model does not have, or that is no switch, is one the
         * game never turns off */
        r->switch_visible[0] = r->switch_visible[1] = 0xffffffffu;
        for (k = 0; k < hdr->numSwitches && k < SL_VM_MAX_SWITCH; k++) {
            const ModelNode *nd = hdr->Switches[k];
            union ModelRwData *rw;
            if (nd == NULL || (nd->Opcode & 0xff) != MODELNODE_OPCODE_SWITCH) continue;
            rw = modelGetNodeRwData(model, (ModelNode *) nd);
            if (rw != NULL && !rw->Switch.visible)
                r->switch_visible[k >> 5] &= ~(1u << (k & 31));
        }
        gdl->words.w0 = (u32) SL_VM_DL_W0;
        gdl->words.w1 = (u32) r;
        mr->gdl = gdl + 1;
        g_emitted++;
        g_ctx.record = r;
    }
    g_ctx.state = ST_REPLACED;
    g_ctx.active = 1;
}

/* SL_WATCH_SLEEVES: one (the default) | all | native. */
static int watch_mode(void)
{
    static int mode = -2;
    if (mode == -2) {
        const char *v = getenv("SL_WATCH_SLEEVES");
        mode = SL_VM_ALT_ONE;
        if (v != NULL && strcmp(v, "all") == 0) mode = SL_VM_ALT_ALL;
        else if (v != NULL && strcmp(v, "native") == 0) mode = -1;
        if (v != NULL)
            fprintf(stderr, "sl_vmasset: watch sleeves %s\n",
                    mode == SL_VM_ALT_ALL ? "ALL (the source's composite)"
                    : mode < 0 ? "NATIVE (the game's watch hand)" : "ONE (the costume's)");
    }
    return mode;
}

void sl_vm_watch_begin(void *modelv, ModelRenderData *mr)
{
    Model *model = (Model *) modelv;
    const ModelFileHeader *hdr;
    const void *asset;
    const struct sl_chr_map *map;
    char key[SL_VM_KEY_MAX];
    int mode, k, cuff = -1, cuffs = 0;

    memset(&g_ctx, 0, sizeof g_ctx);
    if (!sl_bond_assets_on() || !sl_wa_active()) return;
    if (model == NULL || model->obj == NULL || mr == NULL || model->render_pos == NULL) return;
    mode = watch_mode();
    if (mode < 0) return;
    hdr = model->obj;
    g_ctx.watch = pres_on();
    g_ctx.hand = 1;
    g_ctx.item = ITEM_SUIT_LF_HAND;
    g_ctx.state = ST_MISS;
    g_ctx.root = hdr->RootNode;
    if (!key_of(get_ptr_item_text_call_line(ITEM_SUIT_LF_HAND), 'C', key)) return;
    asset = sl_wa_lookup(SL_CHR_CLASS_GUN, key);
    if (asset == NULL) return;
    map = map_for(hdr, key, 'C', asset);
    if (map == NULL) { g_ctx.state = ST_REFUSED; return; }
    /* the costume's cuff: the one of Switches[4..9] bondviewSelectCuff left
     * visible this frame */
    for (k = SL_VM_WATCH_CUFF_FIRST; k < SL_VM_WATCH_CUFF_FIRST + SL_VM_WATCH_CUFFS
                                     && k < hdr->numSwitches; k++) {
        const ModelNode *nd = hdr->Switches[k];
        union ModelRwData *rw;
        if (nd == NULL || (nd->Opcode & 0xff) != MODELNODE_OPCODE_SWITCH) continue;
        rw = modelGetNodeRwData(model, (ModelNode *) nd);
        if (rw != NULL && rw->Switch.visible) { cuff = k; cuffs++; }
    }
    if (mode == SL_VM_ALT_ONE && (cuffs != 1 || !sl_vm_alt_serves(asset, cuff))) {
        static int told[SL_VM_WATCH_CUFF_FIRST + SL_VM_WATCH_CUFFS + 1];
        int t = (cuffs == 1) ? cuff : SL_VM_WATCH_CUFF_FIRST + SL_VM_WATCH_CUFFS;
        if (!told[t]) {
            told[t] = 1;
            if (cuffs == 1)
                fprintf(stderr, "sl_vmasset: gun/%s - no sleeve serves the costume's cuff "
                                "(Switches[%d]); the game's watch hand draws\n", key, cuff);
            else
                fprintf(stderr, "sl_vmasset: gun/%s - %d costume cuffs visible; the game's "
                                "watch hand draws\n", key, cuffs);
        }
        g_ctx.state = ST_NOSLEEVE;
        return;
    }
    emit(model, hdr, mr, asset, map, mode);
    if (g_ctx.record != NULL) ((struct sl_vm_draw *) g_ctx.record)->release_depth = 1;
}

/* THE WEAPON ON THE WATCH: the item the watch draws (`item`, as the watch
 * passes it), its model the right hand's buffer as the watch posed it. */
void sl_vm_menu_begin(int item, void *modelv, ModelRenderData *mr)
{
    Model *model = (Model *) modelv;
    const ModelFileHeader *hdr;
    const void *asset;
    const struct sl_chr_map *map;
    struct sl_vm_draw *r;
    char key[SL_VM_KEY_MAX];

    memset(&g_ctx, 0, sizeof g_ctx);
    if (!sl_bond_assets_on() || !sl_wa_active()) return;
    if (model == NULL || model->obj == NULL || mr == NULL || model->render_pos == NULL) return;
    hdr = model->obj;
    g_ctx.watch = pres_on();
    g_ctx.hand = GUNRIGHT;
    g_ctx.item = item;
    g_ctx.state = ST_MISS;
    g_ctx.root = hdr->RootNode;
    if (!key_of(get_ptr_item_text_call_line((ITEM_IDS) item), 'G', key)) return;
    asset = sl_wa_lookup(SL_CHR_CLASS_GUN, key);
    if (asset == NULL) return;
    map = map_for(hdr, key, 'G', asset);
    if (map == NULL) { g_ctx.state = ST_REFUSED; return; }
    emit(model, hdr, mr, asset, map, SL_VM_ALT_ONE);
    g_ctx.flash = (hdr->numSwitches > SL_VM_FLASH) ? hdr->Switches[SL_VM_FLASH] : NULL;
    r = (struct sl_vm_draw *) g_ctx.record;
    if (r != NULL) {
        sl_vm_menu_look((int) mr->PropType, (unsigned) mr->envcolour.word,
                        (unsigned) mr->fogcolour.word, r->shade, &r->fade);
        r->release_depth = 1;      /* a page drawn after it is not cut by it */
    }
}

void sl_vm_asset_end(void)
{
    if (g_ctx.watch) report();
    g_ctx.active = 0;
    g_ctx.watch = 0;
}

int sl_vm_asset_skip(const void *nodev)
{
    const ModelNode *node = (const ModelNode *) nodev;
    const ModelNode *n, *top = node;
    int guard = 0;
    if (!(g_ctx.active || g_ctx.watch) || node == NULL) return 0;
    for (n = node; n != NULL && guard++ < 64; n = n->Parent) {
        if (g_ctx.flash != NULL && n == g_ctx.flash) { g_ctx.kept++; return 0; }
        top = n;
    }
    if (top != g_ctx.root) return 0;
    if (!g_ctx.active) { g_ctx.kept++; return 0; }
    g_ctx.withheld++;
    g_withheld++;
    return 1;
}

int sl_vm_draw_ok(const void *p)
{
    const unsigned char *b = (const unsigned char *) p;
    const unsigned char *lo = (const unsigned char *) &g_ring[0];
    const unsigned char *hi = (const unsigned char *) &g_ring[SL_VM_RING];
    if (b < lo || b >= hi) return 0;
    if ((unsigned) (b - lo) % sizeof g_ring[0] != 0) return 0;
    return ((const struct sl_vm_draw *) p)->magic == SL_VM_DRAW_MAGIC;
}

void sl_vm_asset_report(void *f)
{
    fprintf(f, "sl_vmasset: bridge draws=%u withheld native lists=%u maps=%d\n",
            g_emitted, g_withheld, g_nmaps);
}

#endif /* __sgi */
