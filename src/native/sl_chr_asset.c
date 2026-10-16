/**
 * sl_chr_asset.c - the CHARACTER-ASSET seam (#70), game side. See
 * src/sl_chr_asset.h for the contract.
 *
 * WHAT THIS FILE DECIDES, per character draw:
 *
 *   1. whether the presentation carries a replacement for the body and for
 *      the head the character wears - the provider is asked by the model's
 *      own name (Cgreatguard2Z -> char/greatguard2, CheadleeZ ->
 *      head/headlee), so ORIGINAL and COMMUNITY HD, which select no world
 *      assets, ask nothing at all;
 *   2. whether the replacement rides this model's skeleton - the game
 *      model's GROUP tree is read once per model file and handed to
 *      sl_chr_derive with the item; a refusal is logged once and the part
 *      draws as the game's;
 *   3. whether the two parts COMPOSE - see THE NECK below;
 *   4. where the replacement is drawn - one bridge command at the start of
 *      the character's geometry, in the opaque pass - and which of the
 *      game's own display lists are withheld: those under the model's root
 *      and outside its head (the body), and those under its head node (the
 *      head), each only when that part is replaced.
 *
 * THE NECK. The two vocabularies put the neck in different parts. The game's
 * body model carries the neck in its own lists, and a separate head file is
 * the head alone. A replacement body carries its neck INSIDE its built-in
 * head block (the gated draws marked
 * SL_WA_MESH_BUILTIN_HEAD: neck and face together), and a replacement head
 * item carries a neck of its own, down to the same base under the collar.
 * So when a character wears a separate head, the neck belongs to whichever
 * head is drawn: a replacement head brings its own, and the game's own head
 * relies on the game's own body for it. The one pairing that leaves nothing
 * owning the neck is a replacement body - its built-in head block withheld
 * for the separate head - under the game's own head. That pairing is never
 * composed: a character that wears a separate head with no replacement keeps
 * the game's body too, and draws exactly as the game's. The reverse pairing
 * composes - the game's body keeps its neck, and the replacement head's own
 * neck closes over it into the game's collar.
 *
 * WHAT IT DOES NOT DO: write any game field, allocate game memory, touch the
 * RNG, or change which nodes the render walk visits. The walk still applies
 * every relation it applies today (distance LOD, switches, the head
 * attachment); modelRenderNodeGundl / modelRenderNodeDl ask sl_chr_asset_skip
 * only before they EMIT, so the only thing a replaced part loses is its
 * gSPDisplayList. The per-tick trace is the check that says so.
 *
 * LIFETIME. A draw record lives in a ring of SL_CHR_RING entries, written as
 * the frame's display list is built and read when it is interpreted; the ring
 * is far longer than any frame's character count, and the renderer validates
 * every record it is handed (sl_chr_draw_ok). Joint maps are derived once per
 * (model file, item) and kept for the session; items are the provider's and
 * outlive every map. Leaving XBLA makes the provider answer nothing, so no
 * map is consulted and nothing is withheld on the very next draw.
 *
 * SL_CHR_ASSETS=0 is the control: the seam stays dark under every
 * presentation, and the character draws exactly as the game's.
 *
 * THE STAGE'S RIGID PROPS (a mainframe, a desk, a monitor, a vehicle) draw
 * through the same bridge and ring from their own entry, sl_chr_prop_begin/
 * end, around the object render's subdraw (propobj.c); see THE STAGE'S RIGID
 * PROPS below and in src/sl_chr_asset.h. SL_PROP_ASSETS=0 is their control.
 */
#ifndef __sgi

#include <ultra64.h>
#include <bondgame.h>
#include "chr.h"
#include "chrobjdata.h"
#include "../sl_chr_asset.h"

extern int    fprintf(void *, const char *, ...);
extern void  *stderr;
extern char  *getenv(const char *);
extern void  *memset(void *, int, unsigned);
extern int    strcmp(const char *, const char *);

extern int         sl_wa_active(void);
extern const void *sl_wa_lookup(unsigned asset_class, const char *path);
extern const char *sl_wa_set(void);
extern int         sl_presentation_prop_fit(const char *world_set, const char *item);
extern int         sl_vm_asset_skip(const void *node);

#define SL_CHR_RING       256
#define SL_CHR_MAPS       32
#define SL_CHR_TREES      16
#define SL_CHR_ITEM_MAX   32

static struct sl_chr_draw g_ring[SL_CHR_RING];
static unsigned g_ring_at;

struct map_slot {
    const void *hdr;
    const void *asset;
    int         ok;
    struct sl_chr_map map;
};
static struct map_slot g_maps[SL_CHR_MAPS];
static int g_nmaps;

struct tree_slot {
    const void *hdr;
    struct sl_chr_tree tree;
    int ok;
};
static struct tree_slot g_trees[SL_CHR_TREES];
static int g_ntrees;

/* What the seam decided for one part, for the SL_CHR_PRES report. */
#define PART_NONE      0     /* no such part (no separate head worn)        */
#define PART_MISS      1     /* the presentation carries no item for it     */
#define PART_REFUSED   2     /* an item exists; it does not ride the model  */
#define PART_HELD      3     /* a body item held back: the worn head has no
                                replacement, so nothing would own the neck  */
#define PART_REPLACED  4     /* drawn from the item                         */
#define PART_OWNED     5     /* a worn head the body item's own head stands
                                for (sl_chr_item_owns_head): withheld      */

static struct {
    int active;
    int watch;                              /* SL_CHR_PRES: count, report   */
    const ModelNode *root;
    int skip[SL_CHR_PARTS];
    const ChrRecord *chr;
    int pass;
    int body, head;                         /* the files the draw resolved */
    int state[SL_CHR_PARTS];
    const void *record;
    unsigned kept[SL_CHR_PARTS], withheld[SL_CHR_PARTS];
} g_ctx;

/* THE WORN HAT (src/sl_chr_asset.h): the hat model the character's walk
 * draws on its head, while an item stands for it. */
static struct {
    int on;
    const ModelNode *root;
    struct sl_chr_draw *rec;
    const void *asset;                       /* the hat item and its map */
    const struct sl_chr_map *map;
    const ModelFileHeader *hdr;
    const char *file;
    int base;                                /* the body matrix it rides */
} g_hat;
static void hat_begin(ChrRecord *chr, ModelRenderData *mr);
static void hat_seat(ChrRecord *chr, const void *head, const struct sl_chr_map *head_map);

/* THE HELD WEAPON (src/sl_chr_asset.h, THE COLLECTABLE): the weapon models
 * the character's walk draws in its hands, while an item stands for each. */
static struct {
    int on;
    const ModelNode *root, *flash[2];
} g_held[2];
static void held_begin(ChrRecord *chr, ModelRenderData *mr);

static unsigned g_emitted, g_withheld;

/* THE PLAYER'S EXTERNAL BODY (src/sl_chr_asset.h): the model and header the
 * game built it from, and the body and head it chose. */
static struct { const void *model, *hdr; int body, head; } g_player = { NULL, NULL, -1, -1 };

extern int sl_bond_assets_on(void);
extern int sl_chr_item_owns_head(const void *asset);
extern HATTYPE get_hat_model(PropRecord *prop);        /* propobj.c */
extern s32 modelFindNodeMtxIndex(ModelNode *node, s32 arg1);   /* model.c */
static int pres_on(void);

void sl_chr_player_model(const void *model, const void *hdr, int body, int head)
{
    g_player.model = model;
    g_player.hdr = hdr;
    g_player.body = body;
    g_player.head = head;
    if (pres_on())
        fprintf(stderr, "sl_chrpres: player body model=%p body=%d(%s) head=%d(%s)\n",
                model, body, (body >= 0 && c_item_entries[body].filename) ? c_item_entries[body].filename : "-",
                head, (head >= 0 && c_item_entries[head].filename) ? c_item_entries[head].filename : "-");
}

static int seam_on(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_CHR_ASSETS");
                  on = (v == NULL || *v != '0'); }
    return on;
}

/* SL_CHR_PRES=1: one line per character and pass whenever what the seam
 * decided or emitted for it changes (the same switch as the character
 * presentation witness in sl_chr_watch.c). Read-only. */
static int pres_on(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_CHR_PRES");
                  on = (v != NULL && *v != '\0' && *v != '0'); }
    return on;
}

/* The model's own name as an item path: "C" + item + "Z", the item lowered.
 * Anything else is not a character model file and gets no item. */
static int item_of(const char *file, char *out)
{
    int n = 0, i;
    if (file == NULL || file[0] != 'C') return 0;
    while (file[n] != '\0' && n < 64) n++;
    if (n < 3 || n - 2 >= SL_CHR_ITEM_MAX || file[n - 1] != 'Z') return 0;
    for (i = 1; i < n - 1; i++) {
        char c = file[i];
        if (c >= 'A' && c <= 'Z') c = (char) (c - 'A' + 'a');
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return 0;
        out[i - 1] = c;
    }
    out[n - 2] = '\0';
    return 1;
}

/* The body's GROUP tree in walk order, the head's subtree left out. */
static int gather(const ModelFileHeader *hdr, struct sl_chr_tree *t)
{
    const ModelNode *nodes[SL_CHR_MAX_JOINTS];
    const ModelNode *nd = hdr->RootNode;
    int guard = 0;

    memset(t, 0, sizeof *t);
    while (nd != NULL && guard++ < 4096) {
        int op = nd->Opcode & 0xff;
        if (op == MODELNODE_OPCODE_GROUP && nd->Data != NULL) {
            const ModelNode *up;
            int p = -1, k;
            if (t->count >= SL_CHR_MAX_JOINTS) return 0;
            for (up = nd->Parent; up != NULL && p < 0; up = up->Parent)
                for (k = 0; k < t->count; k++)
                    if (nodes[k] == up) { p = k; break; }
            nodes[t->count] = nd;
            t->parent[t->count] = p;
            t->origin[t->count][0] = nd->Data->Group.Origin.f[0];
            t->origin[t->count][1] = nd->Data->Group.Origin.f[1];
            t->origin[t->count][2] = nd->Data->Group.Origin.f[2];
            t->mtx[t->count] = nd->Data->Group.MatrixID0;
            if (t->mtx[t->count] < 0 || t->mtx[t->count] >= hdr->numMatrices) return 0;
            t->count++;
        }
        /* never into the head: its nodes are the head file's */
        if (nd->Child != NULL && op != MODELNODE_OPCODE_HEAD) { nd = nd->Child; continue; }
        while (nd != NULL && nd != hdr->RootNode && nd->Next == NULL) nd = nd->Parent;
        if (nd == NULL || nd == hdr->RootNode) break;
        nd = nd->Next;
    }
    return t->count > 0;
}

/* `key` is the model FILE's identity - its header in c_item_entries - and
 * `hdr` the header the character is drawn from: the same pointer for every
 * character but the player, whose header is a private copy that the view
 * model's buffer reuses between lives, so a cache keyed by it could answer
 * for a different file. */
static const struct sl_chr_tree *tree_for(const void *key, const ModelFileHeader *hdr)
{
    int i;
    for (i = 0; i < g_ntrees; i++)
        if (g_trees[i].hdr == key)
            return g_trees[i].ok ? &g_trees[i].tree : NULL;
    if (g_ntrees >= SL_CHR_TREES) return NULL;
    i = g_ntrees++;
    g_trees[i].hdr = key;
    g_trees[i].ok = gather(hdr, &g_trees[i].tree);
    return g_trees[i].ok ? &g_trees[i].tree : NULL;
}

static const struct sl_chr_map *map_for(const void *key, const ModelFileHeader *hdr,
                                        const char *file,
                                        const void *asset, const char *what)
{
    const struct sl_chr_tree *t;
    char why[96];
    int i, j;
    for (i = 0; i < g_nmaps; i++)
        if (g_maps[i].hdr == key && g_maps[i].asset == asset)
            return g_maps[i].ok ? &g_maps[i].map : NULL;
    if (g_nmaps >= SL_CHR_MAPS) return NULL;
    i = g_nmaps++;
    g_maps[i].hdr = key;
    g_maps[i].asset = asset;
    t = tree_for(key, hdr);
    if (t == NULL) {
        g_maps[i].ok = 0;
        fprintf(stderr, "sl_chrasset: %s on %s REFUSED - the model's skeleton could not be "
                        "read; the game's model draws\n", what, file);
        return NULL;
    }
    g_maps[i].ok = sl_chr_derive(asset, t, &g_maps[i].map, why);
    if (!g_maps[i].ok) {
        fprintf(stderr, "sl_chrasset: %s on %s REFUSED - %s; the game's model draws\n",
                what, file, why);
        return NULL;
    }
    fprintf(stderr, "sl_chrasset: %s rides %s - %d item joints on %d game joints, "
                    "scale %.3f, turns",
            what, file, g_maps[i].map.count, t->count, (double) g_maps[i].map.scale);
    for (j = 0; j < g_maps[i].map.count; j++)
        fprintf(stderr, " %d:%d/%d", j, g_maps[i].map.mtx[j], g_maps[i].map.rot[j]);
    fprintf(stderr, "\n");
    return &g_maps[i].map;
}

void sl_chr_asset_begin(ChrRecord *chr, ModelRenderData *mr)
{
    const ModelFileHeader *hdr;
    const void *asset[SL_CHR_PARTS] = { NULL, NULL };
    const struct sl_chr_map *map[SL_CHR_PARTS] = { NULL, NULL };
    char bitem[SL_CHR_ITEM_MAX], hitem[SL_CHR_ITEM_MAX];
    int body, head, wears_head;
    unsigned parts;
    struct sl_chr_draw *r;
    const void *key, *own = NULL;
    int player;

    g_ctx.active = 0;
    g_ctx.watch = 0;
    g_hat.on = 0;
    g_hat.rec = NULL;
    g_held[0].on = g_held[1].on = 0;
    if (!seam_on() || !sl_wa_active()) return;
    if (chr == NULL || mr == NULL || chr->model == NULL || chr->model->obj == NULL) return;
    if (mr->unk18 != 0) return;                   /* a sorting walk emits nothing */
    /* PropType 8 is a character the game is FADING: it draws as the game's -
     * except the player's body, whose every external appearance (the intro
     * swirl, the death camera) ends in the game's fade, and which the
     * renderer fades by the same alpha (chr_asset_draw, THE GAME'S FADE) */
    player = chr->prop != NULL && chr->prop->type == PROP_TYPE_VIEWER;
    if (mr->PropType != 7 && !(player && mr->PropType == 8)) return;
    body = (int) chr->bodynum;
    head = (int) chr->headnum;
    hdr = chr->model->obj;
    key = hdr;
    if (player) {
        /* the player's external body: the game's own choice of files, as
         * solo_char_load handed it over (THE PLAYER'S EXTERNAL BODY) */
        if (!sl_bond_assets_on() || g_player.model != (const void *) chr->model
            || g_player.hdr != (const void *) hdr) return;
        body = g_player.body;
        head = g_player.head;
        if (body < 0) return;
        key = c_item_entries[body].header;
    } else {
        if (body < 0) return;
        if (c_item_entries[body].header != hdr) return;
    }
    if (chr->model->render_pos == NULL) return;
    hat_begin(chr, mr);
    held_begin(chr, mr);
    wears_head = (head >= 0 && !c_item_entries[body].hasHead);

    if (pres_on()) {
        memset(&g_ctx, 0, sizeof g_ctx);
        g_ctx.watch = 1;
        g_ctx.chr = chr;
        g_ctx.body = body;
        g_ctx.head = head;
        g_ctx.root = hdr->RootNode;
        g_ctx.pass = mr->flags & 3;
        g_ctx.state[SL_CHR_PART_BODY] = PART_MISS;
        g_ctx.state[SL_CHR_PART_HEAD] = wears_head ? PART_MISS : PART_NONE;
    }

    /* A body item that carries its OWN head stands for the body and the
     * head the model wears (sl_chr_item_owns_head): nothing is composed. */
    if (item_of(c_item_entries[body].filename, bitem)) {
        const void *b = sl_wa_lookup(SL_CHR_CLASS_CHAR, bitem);
        if (b != NULL && sl_chr_item_owns_head(b)) own = b;
    }
    if (own != NULL) {
        map[SL_CHR_PART_BODY] = map_for(key, hdr, c_item_entries[body].filename, own,
                                        "char item (its own head)");
        if (map[SL_CHR_PART_BODY] == NULL) {
            g_ctx.state[SL_CHR_PART_BODY] = PART_REFUSED;
            return;
        }
        asset[SL_CHR_PART_BODY] = own;
        g_ctx.state[SL_CHR_PART_BODY] = PART_REPLACED;
        if (wears_head) g_ctx.state[SL_CHR_PART_HEAD] = PART_OWNED;
        parts = SL_CHR_DRAW_BODY | SL_CHR_DRAW_BUILTIN_HEAD;
        if (wears_head) hat_seat(chr, own, map[SL_CHR_PART_BODY]);
        goto emit;
    }

    /* The head first: whether it is replaced decides whether the body may
     * be (THE NECK, above; sl_chr_compose holds the rule). */
    if (wears_head && item_of(c_item_entries[head].filename, hitem))
        asset[SL_CHR_PART_HEAD] = sl_wa_lookup(SL_CHR_CLASS_HEAD, hitem);
    if (asset[SL_CHR_PART_HEAD] != NULL) {
        map[SL_CHR_PART_HEAD] = map_for(key, hdr, c_item_entries[body].filename,
                                        asset[SL_CHR_PART_HEAD], "head item");
        if (map[SL_CHR_PART_HEAD] == NULL) {
            asset[SL_CHR_PART_HEAD] = NULL;
            g_ctx.state[SL_CHR_PART_HEAD] = PART_REFUSED;
        }
    }
    if (sl_chr_compose(wears_head, 1, asset[SL_CHR_PART_HEAD] != NULL) & SL_CHR_DRAW_BODY) {
        if (item_of(c_item_entries[body].filename, bitem))
            asset[SL_CHR_PART_BODY] = sl_wa_lookup(SL_CHR_CLASS_CHAR, bitem);
    } else if (g_ctx.watch && item_of(c_item_entries[body].filename, bitem)
               && sl_wa_lookup(SL_CHR_CLASS_CHAR, bitem) != NULL) {
        g_ctx.state[SL_CHR_PART_BODY] = PART_HELD;   /* the report only */
    }
    if (asset[SL_CHR_PART_BODY] != NULL) {
        map[SL_CHR_PART_BODY] = map_for(key, hdr, c_item_entries[body].filename,
                                        asset[SL_CHR_PART_BODY], "char item");
        if (map[SL_CHR_PART_BODY] == NULL) {
            asset[SL_CHR_PART_BODY] = NULL;
            g_ctx.state[SL_CHR_PART_BODY] = PART_REFUSED;
        }
    }
    parts = sl_chr_compose(wears_head, asset[SL_CHR_PART_BODY] != NULL,
                           asset[SL_CHR_PART_HEAD] != NULL);
    if (!(parts & SL_CHR_DRAW_BODY)) asset[SL_CHR_PART_BODY] = NULL;
    if (!(parts & SL_CHR_DRAW_HEAD)) asset[SL_CHR_PART_HEAD] = NULL;
    if (asset[SL_CHR_PART_BODY] != NULL) g_ctx.state[SL_CHR_PART_BODY] = PART_REPLACED;
    if (asset[SL_CHR_PART_HEAD] != NULL) g_ctx.state[SL_CHR_PART_HEAD] = PART_REPLACED;
    if (asset[SL_CHR_PART_HEAD] != NULL)
        hat_seat(chr, asset[SL_CHR_PART_HEAD], map[SL_CHR_PART_HEAD]);
    if (parts == 0) return;

emit:
    if (mr->flags & 1) {
        Gfx *gdl = mr->gdl;
        r = &g_ring[g_ring_at++ % SL_CHR_RING];
        memset(r, 0, sizeof *r);
        r->magic = SL_CHR_DRAW_MAGIC;
        r->asset[SL_CHR_PART_BODY] = asset[SL_CHR_PART_BODY];
        r->asset[SL_CHR_PART_HEAD] = asset[SL_CHR_PART_HEAD];
        r->map[SL_CHR_PART_BODY] = map[SL_CHR_PART_BODY];
        r->map[SL_CHR_PART_HEAD] = map[SL_CHR_PART_HEAD];
        r->mtx = (const void *) chr->model->render_pos;
        r->mtx_count = hdr->numMatrices;
        r->shade[0] = (unsigned char) ((mr->fogcolour.word >> 24) & 0xff);
        r->shade[1] = (unsigned char) ((mr->fogcolour.word >> 16) & 0xff);
        r->shade[2] = (unsigned char) ((mr->fogcolour.word >> 8) & 0xff);
        r->shade[3] = (unsigned char) (mr->fogcolour.word & 0xff);
        r->wears_head = !(parts & SL_CHR_DRAW_BUILTIN_HEAD);
        r->fade = (mr->PropType == 8) ? (unsigned char) (mr->envcolour.word & 0xff) : 255;
        gdl->words.w0 = (u32) SL_CHR_DL_W0;
        gdl->words.w1 = (u32) r;
        mr->gdl = gdl + 1;
        g_emitted++;
        g_ctx.record = r;
    }
    g_ctx.active = 1;
    g_ctx.root = hdr->RootNode;
    g_ctx.skip[SL_CHR_PART_BODY] = asset[SL_CHR_PART_BODY] != NULL;
    g_ctx.skip[SL_CHR_PART_HEAD] = asset[SL_CHR_PART_HEAD] != NULL
                                 || (own != NULL && wears_head);
}

/* The SL_CHR_PRES seam line: printed when what a character's pass decided or
 * emitted differs from the last line printed for it. */
#define SL_CHR_REPORT_SLOTS  64
#define SL_CHR_REPORT_LINES  512
static void report(void)
{
    static const char *const nm[] = { "-", "MISS", "REFUSED", "HELD", "REPLACED", "OWNED" };
    static struct { const void *chr; int pass; unsigned key; } last[SL_CHR_REPORT_SLOTS];
    static unsigned lines;
    const ChrRecord *c = g_ctx.chr;
    int body = g_ctx.body, head = g_ctx.head, i, at = -1;
    unsigned key = 2166136261u;
    unsigned v[8];

    v[0] = (unsigned) g_ctx.state[0]; v[1] = (unsigned) g_ctx.state[1];
    v[2] = g_ctx.kept[0]; v[3] = g_ctx.withheld[0];
    v[4] = g_ctx.kept[1]; v[5] = g_ctx.withheld[1];
    v[6] = (unsigned) (g_ctx.record != NULL); v[7] = (unsigned) g_ctx.pass;
    for (i = 0; i < 8; i++) key = (key ^ v[i]) * 16777619u;
    for (i = 0; i < SL_CHR_REPORT_SLOTS; i++)
        if (last[i].chr == (const void *) c && last[i].pass == g_ctx.pass) { at = i; break; }
    if (at < 0)
        for (i = 0; i < SL_CHR_REPORT_SLOTS; i++)
            if (last[i].chr == NULL) { at = i; break; }
    if (at < 0) return;
    if (last[at].chr == (const void *) c && last[at].key == key) return;
    last[at].chr = (const void *) c; last[at].pass = g_ctx.pass; last[at].key = key;
    if (lines++ >= SL_CHR_REPORT_LINES) return;
    fprintf(stderr, "sl_chrpres: seam chrnum=%d pass=%d body=%s:%s head=%s:%s bridge=%p "
                    "native lists body kept=%u withheld=%u head kept=%u withheld=%u\n",
            (int) c->chrnum, g_ctx.pass,
            c_item_entries[body].filename ? c_item_entries[body].filename : "-",
            nm[g_ctx.state[SL_CHR_PART_BODY]],
            (head >= 0 && g_ctx.state[SL_CHR_PART_HEAD] != PART_NONE
             && c_item_entries[head].filename) ? c_item_entries[head].filename : "-",
            nm[g_ctx.state[SL_CHR_PART_HEAD]],
            g_ctx.record, g_ctx.kept[SL_CHR_PART_BODY], g_ctx.withheld[SL_CHR_PART_BODY],
            g_ctx.kept[SL_CHR_PART_HEAD], g_ctx.withheld[SL_CHR_PART_HEAD]);
}

void sl_chr_asset_end(void)
{
    if (g_ctx.watch) report();
    g_ctx.active = 0;
    g_ctx.watch = 0;
    g_hat.on = 0;
    g_hat.rec = NULL;
    g_held[0].on = g_held[1].on = 0;
}

static int prop_skip_any(const void *nodev);

int sl_chr_asset_skip(const void *nodev)
{
    const ModelNode *node = (const ModelNode *) nodev;
    const ModelNode *n, *top = node;
    int under_head = 0, guard = 0, part;

    /* THE WORN HAT: every list of the hat's own model is withheld */
    if (g_hat.on && node != NULL) {
        for (n = node; n->Parent != NULL && guard++ < 64; n = n->Parent) {}
        if (n == g_hat.root) { g_withheld++; return 1; }
        guard = 0;
    }
    /* THE HELD WEAPON: every list of the weapon's model but its flash */
    if ((g_held[0].on || g_held[1].on) && node != NULL) {
        int h, flash = 0;
        for (n = node; ; n = n->Parent) {           /* to the model's top node */
            for (h = 0; h < 2; h++)
                if (g_held[h].on && (n == g_held[h].flash[0] || n == g_held[h].flash[1])) flash = 1;
            if (n->Parent == NULL || guard++ >= 64) break;
        }
        for (h = 0; h < 2; h++)
            if (g_held[h].on && n == g_held[h].root) {
                if (flash) return 0;
                g_withheld++;
                return 1;
            }
        guard = 0;
    }
    /* No character is being drawn: the one other model this hook can be
     * asked about is the first-person view model (src/native/sl_vm_asset.c). */
    if (!(g_ctx.active || g_ctx.watch)) {
        int got = prop_skip_any(nodev);
        return got >= 0 ? got : sl_vm_asset_skip(nodev);
    }
    if (node == NULL) return 0;
    for (n = node->Parent; n != NULL && guard++ < 64; n = n->Parent) {
        if ((n->Opcode & 0xff) == MODELNODE_OPCODE_HEAD) under_head = 1;
        top = n;
    }
    if (top != g_ctx.root) return 0;
    part = under_head ? SL_CHR_PART_HEAD : SL_CHR_PART_BODY;
    if (!g_ctx.active || !g_ctx.skip[part]) { g_ctx.kept[part]++; return 0; }
    g_ctx.withheld[part]++;
    g_withheld++;
    return 1;
}

int sl_chr_draw_ok(const void *p)
{
    const unsigned char *b = (const unsigned char *) p;
    const unsigned char *lo = (const unsigned char *) &g_ring[0];
    const unsigned char *hi = (const unsigned char *) &g_ring[SL_CHR_RING];
    if (b < lo || b >= hi) return 0;
    if ((unsigned) (b - lo) % sizeof g_ring[0] != 0) return 0;
    return ((const struct sl_chr_draw *) p)->magic == SL_CHR_DRAW_MAGIC;
}

/* ---- THE STAGE'S RIGID PROPS (src/sl_chr_asset.h) ---------------------
 *
 * The prop's model draws through the same bridge and the same ring as a
 * character; what differs is how the item is related to the model (by the
 * two boxes, sl_prop_derive) and what is withheld: every list under the
 * model's root, but a monitor's screens - Switches[0], or Switches[0..3] on
 * a multi-screen one, the nodes the game animates the screens' text on
 * (propobj.c, process_monitor_animation_microcode; sl_prop_takes) - which go
 * on drawing over the item. A prop the game draws in any damaged state, or
 * one of a type this does not take (doors, glass, pickups), draws as the
 * game's. A VEHICLE is
 * taken: its model moves its wheels under matrices of their own, and its
 * item rides them (THE MOVING PROP); the vehicle's path, speed, wheel turn
 * and steer stay the game's, computed where they always were. */
extern struct ItemModelFileRecord PitemZ_entries[];
extern s32 objGetDestroyedLevel(ObjectRecord *obj);
extern int sl_prop_derive(const void *asset, const struct sl_prop_box *game,
                          struct sl_chr_map *out, char *why);
extern int sl_prop_item_moves(const void *asset);
extern unsigned sl_prop_item_passes(const void *asset);
extern int sl_screen_place(const void *asset, const struct sl_chr_map *map,
                           const float quad[4][3], float out[4][3]);
extern union ModelRwData *modelGetNodeRwData(Model *Objinst, ModelNode *root);

/* SL_PROP_SCREEN_PLACE=0: a monitor's screen stays where the game puts it
 * (THE SCREEN'S PLACE, src/sl_chr_asset.h). */
static int screen_place_on(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_PROP_SCREEN_PLACE");
                  on = (v == NULL || *v != '0'); }
    return on;
}

/* One screen's place, decided once per model file and screen. */
#define SL_SCREEN_PLACES 64
static struct { const void *hdr, *asset; int k, moved; short xyz[4][3]; } g_splace[SL_SCREEN_PLACES];
static int g_nsplace;

static void screen_place(ObjectRecord *obj, const ModelFileHeader *hdr, const void *asset,
                         const struct sl_chr_map *map, int k, const ModelNode *node,
                         const char *file)
{
    const Vertex *rod;
    union ModelRwData *rw;
    int i, at = -1;
    if (node == NULL || (node->Opcode & 0xff) != MODELNODE_OPCODE_DLCOLLISION || node->Data == NULL)
        return;
    rod = node->Data->DisplayListCollisions.Vertices;
    rw = modelGetNodeRwData(obj->model, (ModelNode *) node);
    if (rod == NULL || rw == NULL || rw->DisplayListCollisions.Vertices == NULL
        || rw->DisplayListCollisions.Vertices == rod)
        return;
    for (i = 0; i < g_nsplace; i++)
        if (g_splace[i].hdr == hdr && g_splace[i].asset == asset && g_splace[i].k == k) { at = i; break; }
    if (at < 0) {
        float q[4][3], o[4][3];
        int c;
        if (g_nsplace >= SL_SCREEN_PLACES) return;
        at = g_nsplace++;
        g_splace[at].hdr = hdr;
        g_splace[at].asset = asset;
        g_splace[at].k = k;
        for (i = 0; i < 4; i++)
            for (c = 0; c < 3; c++) q[i][c] = (float) rod[i].coord.AsArray[c];
        g_splace[at].moved = (map->mtx[0] == 0) && sl_screen_place(asset, map, (const float (*)[3]) q, o);
        for (i = 0; i < 4; i++)
            for (c = 0; c < 3; c++) {
                float f = o[i][c];
                g_splace[at].xyz[i][c] = (short) (f < 0.0f ? f - 0.5f : f + 0.5f);
            }
        if (g_splace[at].moved)
            fprintf(stderr, "sl_propasset: screen %d of %s placed on the item's own screen "
                            "rectangle (%d %d %d .. %d %d %d)\n", k, file,
                    g_splace[at].xyz[0][0], g_splace[at].xyz[0][1], g_splace[at].xyz[0][2],
                    g_splace[at].xyz[2][0], g_splace[at].xyz[2][1], g_splace[at].xyz[2][2]);
    }
    if (g_splace[at].moved)
        sl_gfx_screen_place((const void *) rw->DisplayListCollisions.Vertices,
                            (const short (*)[3]) g_splace[at].xyz);
}

/* SL_PROP_TWOPASS=0: a prop item draws whole in the solid walk, as before
 * THE PROP'S TWO PASSES (src/sl_chr_asset.h). */
static int sl_prop_two_pass_on(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_PROP_TWOPASS");
                  on = (v == NULL || *v != '0'); }
    return on;
}

#define SL_PROP_MAPS 64
static struct { const void *hdr, *asset; int ok; struct sl_chr_map map; } g_pmaps[SL_PROP_MAPS];
static int g_npmaps;
static struct {
    int active, watch;
    const ModelNode *root, *keep[SL_PROP_MAX_SCREENS];   /* a monitor's screens */
    const char *file;
    int type, state;
    const void *record;
    unsigned kept, withheld;
    int door_open;                          /* THE DYNAMIC DOOR: SL_DOOR_DL_W0 open */
    int destroyed;                          /* THE DESTROYED AUTOGUN: the game's level */
    int destroyed_mtx;                      /* and the deformed list's matrix, -1 when none */
} g_pctx;

/* A weapon model's muzzle-flash switch `k` (THE COLLECTABLE), or NULL when
 * the model is no weapon's or has none there. */
static const ModelNode *gunfire_switch(const ModelFileHeader *hdr, int k)
{
    if (hdr == NULL || hdr->Skeleton != &skeleton_prop_weapon || k >= hdr->numSwitches)
        return NULL;
    return hdr->Switches[k];
}

/* Is `nd` at or under one of the model's muzzle-flash switches? */
static int under_gunfire(const ModelFileHeader *hdr, const ModelNode *nd)
{
    const ModelNode *a = gunfire_switch(hdr, SL_PROP_GUNFIRE_A);
    const ModelNode *b = gunfire_switch(hdr, SL_PROP_GUNFIRE_B);
    int guard = 0;
    if (a == NULL && b == NULL) return 0;
    for (; nd != NULL && guard++ < 64; nd = nd->Parent)
        if (nd == a || nd == b) return 1;
    return 0;
}

/* A node's origin in MODEL space: its own and every positioning ancestor's
 * GROUP origin summed. */
static void prop_origin(const ModelNode *nd, float o[3])
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
}

/* The model's own vertex box, its root GROUP, and every GROUP by the matrix
 * it is drawn under (sl_prop_box). */
static int prop_box(const ModelFileHeader *hdr, struct sl_prop_box *b)
{
    const ModelNode *nd = hdr->RootNode, *root = NULL, *simple = NULL;
    int guard = 0, any = 0, k;
    memset(b, 0, sizeof *b);
    b->root_mtx = -1;
    b->mtx_count = hdr->numMatrices;
    while (nd != NULL && guard++ < 4096) {
        int op = nd->Opcode & 0xff;
        const Vertex *v = NULL;
        int n = 0;
        if (op == MODELNODE_OPCODE_GROUP && root == NULL && nd->Data != NULL) root = nd;
        if (op == MODELNODE_OPCODE_GROUPSIMPLE && simple == NULL && nd->Data != NULL) simple = nd;
        if (op == MODELNODE_OPCODE_GROUP && nd->Data != NULL) {
            int m = nd->Data->Group.MatrixID0;
            if (m >= 0 && m < SL_PROP_MAX_MTX && !b->node_present[m]) {
                b->node_present[m] = 1;
                prop_origin(nd, b->node_origin[m]);
            }
        }
        /* An animated model - the aircraft - is rooted on a HEADER node, the
         * part its animation positions, drawn under the matrix the node names
         * (model.c, modelFindNodeMtxIndex) at the origin of the GROUPs above
         * it. It positions every other part, so an item joint may ride it.
         * Its rotor is a GROUPSIMPLE, drawn under its Group1 (the same
         * lookup), at that node's own origin. */
        if ((op == MODELNODE_OPCODE_HEADER || op == MODELNODE_OPCODE_GROUPSIMPLE)
            && nd->Data != NULL) {
            int m = op == MODELNODE_OPCODE_HEADER ? nd->Data->Header.MatrixIndex
                                                  : nd->Data->GroupSimple.Group1;
            if (op == MODELNODE_OPCODE_HEADER) b->animated = 1;
            if (m >= 0 && m < SL_PROP_MAX_MTX && !b->node_present[m]) {
                b->node_present[m] = 1;
                prop_origin(nd, b->node_origin[m]);
            }
        }
        if (op == MODELNODE_OPCODE_DL && nd->Data != NULL) {
            v = nd->Data->DisplayList.Vertices; n = nd->Data->DisplayList.numVertices;
        } else if (op == MODELNODE_OPCODE_DLCOLLISION && nd->Data != NULL) {
            v = nd->Data->DisplayListCollisions.Vertices; n = nd->Data->DisplayListCollisions.numVertices;
        }
        if (v != NULL && n > 0 && under_gunfire(hdr, nd)) v = NULL;   /* the flash: the game's */
        if (v != NULL && n > 0) {
            float o[3];
            int i;
            prop_origin(nd, o);
            for (i = 0; i < n; i++)
                for (k = 0; k < 3; k++) {
                    float c = (float) v[i].coord.AsArray[k] + o[k];
                    if (!any || c < b->lo[k]) b->lo[k] = c;
                    if (!any || c > b->hi[k]) b->hi[k] = c;
                    if (k == 2) any = 1;
                }
        }
        if (nd->Child != NULL) { nd = nd->Child; continue; }
        while (nd != NULL && nd != hdr->RootNode && nd->Next == NULL) nd = nd->Parent;
        if (nd == NULL || nd == hdr->RootNode) break;
        nd = nd->Next;
    }
    /* A model with no GROUP at all - a hat - is rooted on a GROUPSIMPLE, whose
     * matrix is its Group1 (model.c reads it there). */
    if (root == NULL && simple != NULL) {
        int m = simple->Data->GroupSimple.Group1;
        if (!any || m < 0 || m >= SL_PROP_MAX_MTX) return 0;
        b->node_present[m] = 1;
        prop_origin(simple, b->node_origin[m]);
        prop_origin(simple, b->root_origin);
        b->root_mtx = m;
        return 1;
    }
    if (!any || root == NULL) return 0;
    prop_origin(root, b->root_origin);
    b->root_mtx = root->Data->Group.MatrixID0;
    return 1;
}

/* SL_PROP_ASSETS=0 is the props' control: the prop seam stays dark under every
 * presentation (the characters' seam is SL_CHR_ASSETS, Bond's SL_BOND_ASSETS). */
static int prop_seam_on(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_PROP_ASSETS");
                  on = (v == NULL || *v != '0'); }
    return on;
}

static int prop_item_of(const char *file, char *out)
{
    int n = 0, i;
    if (file == NULL || file[0] != 'P') return 0;
    while (file[n] != '\0' && n < 64) n++;
    if (n < 3 || n - 2 >= SL_CHR_ITEM_MAX || file[n - 1] != 'Z') return 0;
    for (i = 1; i < n - 1; i++) {
        char c = file[i];
        if (c >= 'A' && c <= 'Z') c = (char) (c - 'A' + 'a');
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return 0;
        out[i - 1] = c;
    }
    out[n - 2] = '\0';
    return 1;
}

/* D-045 A ONE-PIECE ITEM OVER A MODEL THAT MOVES PARTS: only for an item the
 * presentation names, which then rides the model's root matrix whole (its
 * moving parts hold the rest pose); SL_PROP_RIGID=0 refuses it as before. */
static int prop_rigid_ok(const char *file)
{
    static int on = -1;
    char it[SL_CHR_ITEM_MAX];
    if (on < 0) { const char *v = getenv("SL_PROP_RIGID"); on = (v == NULL || *v != '0'); }
    if (!on || !prop_item_of(file, it) || !sl_presentation_prop_rigid(sl_wa_set(), it))
        return 0;
    fprintf(stderr, "sl_propasset: prop item on %s is one rigid piece over a model that "
                    "moves parts - it rides the root matrix whole (D-045)\n", file);
    return 1;
}

/* THE FIXED PARTS (src/sl_chr_asset.h, sl_prop_parts_fixed): the model's
 * matrix-setting nodes, counted over its whole tree. SL_PROP_RIGID=0 refuses
 * the item as before, D-045's switch. */
static int prop_fixed_ok(const ModelFileHeader *hdr, const char *file)
{
    static int on = -1;
    unsigned char seen[SL_PROP_MAX_MTX];
    const ModelNode *nd;
    int guard = 0, moving = 0, simple = 0;
    if (on < 0) { const char *v = getenv("SL_PROP_RIGID"); on = (v == NULL || *v != '0'); }
    if (!on || hdr == NULL || hdr->numMatrices < 2 || hdr->numMatrices > SL_PROP_MAX_MTX)
        return 0;
    memset(seen, 0, sizeof seen);
    nd = hdr->RootNode;
    while (nd != NULL && guard++ < 4096) {
        int op = nd->Opcode & 0xff;
        if (op == MODELNODE_OPCODE_GROUP || op == MODELNODE_OPCODE_OP03
            || op == MODELNODE_OPCODE_HEADER)
            moving++;
        if (op == MODELNODE_OPCODE_GROUPSIMPLE && nd->Data != NULL) {
            int m = nd->Data->GroupSimple.Group1;
            if (m < 0 || m >= hdr->numMatrices) return 0;
            if (!seen[m]) { seen[m] = 1; simple++; }
        }
        if (nd->Child != NULL) { nd = nd->Child; continue; }
        while (nd != NULL && nd != hdr->RootNode && nd->Next == NULL) nd = nd->Parent;
        if (nd == NULL || nd == hdr->RootNode) break;
        nd = nd->Next;
    }
    if (!sl_prop_parts_fixed(hdr->numMatrices, moving, simple)) return 0;
    fprintf(stderr, "sl_propasset: prop item on %s - the model's %d matrices are all fixed "
                    "offsets (GROUPSIMPLE); the one-piece item rides the root matrix\n",
            file, hdr->numMatrices);
    return 1;
}

/* `turn`: the item may be THE TURNED ITEM (src/sl_chr_asset.h) - a key or a
 * collectable, the carried kinds; every other kind is related unturned. */
static const struct sl_chr_map *prop_map(const ModelFileHeader *key, const ModelFileHeader *hdr,
                                         const char *file, const void *asset, int turn)
{
    struct sl_prop_box b;
    char why[96];
    int i;
    for (i = 0; i < g_npmaps; i++)
        if (g_pmaps[i].hdr == key && g_pmaps[i].asset == asset)
            return g_pmaps[i].ok ? &g_pmaps[i].map : NULL;
    if (g_npmaps >= SL_PROP_MAPS) return NULL;
    i = g_npmaps++;
    g_pmaps[i].hdr = key;
    g_pmaps[i].asset = asset;
    if (hdr->numMatrices != 1 && !sl_prop_item_moves(asset) && !prop_fixed_ok(hdr, file)
        && !prop_rigid_ok(file)) {
        g_pmaps[i].ok = 0;
        fprintf(stderr, "sl_propasset: prop item on %s REFUSED - the model moves %d parts; "
                        "the item is one rigid piece; the game's model draws\n",
                file, hdr->numMatrices);
        return NULL;
    }
    if (!prop_box(hdr, &b)) {
        g_pmaps[i].ok = 0;
        fprintf(stderr, "sl_propasset: prop item on %s REFUSED - the model's geometry could "
                        "not be read; the game's model draws\n", file);
        return NULL;
    }
    b.turn_ok = turn;
    {   /* THE FITTED ITEM (D-038): only for an item the presentation names;
         * SL_PROP_FIT=0 refuses it as the box test does */
        static int fit_on = -1;
        char it[SL_CHR_ITEM_MAX];
        if (fit_on < 0) { const char *v = getenv("SL_PROP_FIT"); fit_on = (v == NULL || *v != '0'); }
        b.fit_ok = fit_on && prop_item_of(file, it) && sl_presentation_prop_fit(sl_wa_set(), it);
    }
    g_pmaps[i].ok = sl_prop_derive(asset, &b, &g_pmaps[i].map, why);
    if (!g_pmaps[i].ok) {
        fprintf(stderr, "sl_propasset: prop item on %s REFUSED - %s; the game's model draws\n",
                file, why);
        return NULL;
    }
    fprintf(stderr, "sl_propasset: prop item rides %s - scale %.4f, model box %.1f %.1f %.1f .. "
                    "%.1f %.1f %.1f, root matrix %d",
            file, (double) g_pmaps[i].map.scale, (double) b.lo[0], (double) b.lo[1],
            (double) b.lo[2], (double) b.hi[0], (double) b.hi[1], (double) b.hi[2],
            g_pmaps[i].map.mtx[0]);
    if (g_pmaps[i].map.count > 1) {
        int j;
        fprintf(stderr, ", %d joints on matrices", g_pmaps[i].map.count);
        for (j = 0; j < g_pmaps[i].map.count; j++)
            fprintf(stderr, " %d", g_pmaps[i].map.mtx[j]);
    }
    fprintf(stderr, "\n");
    return &g_pmaps[i].map;
}

/* The bridge record for a prop item riding `obj`'s model: the item, its map,
 * the model's matrices and the prop's shade. NULL when the walk emits
 * nothing (mr->flags). */
static struct sl_chr_draw *prop_emit(ObjectRecord *obj, const ModelFileHeader *hdr,
                                     const void *asset, const struct sl_chr_map *map,
                                     ModelRenderData *mr)
{
    struct sl_chr_draw *r;
    Gfx *gdl;
    unsigned passes = 0;
    /* THE PROP'S TWO PASSES (src/sl_chr_asset.h): the record draws the
     * item's draws of this walk; a walk the item has none for emits nothing */
    if (sl_prop_two_pass_on()) {
        passes = (unsigned) mr->flags & sl_prop_item_passes(asset)
                 & (SL_CHR_PASS_SOLID | SL_CHR_PASS_TRANSLUCENT);
        if (passes == 0u) return NULL;
    } else if (!(mr->flags & 1)) return NULL;
    gdl = mr->gdl;
    r = &g_ring[g_ring_at++ % SL_CHR_RING];
    memset(r, 0, sizeof *r);
    r->magic = SL_CHR_DRAW_MAGIC;
    r->asset[SL_CHR_PART_BODY] = asset;
    r->map[SL_CHR_PART_BODY] = map;
    r->mtx = (const void *) obj->model->render_pos;
    r->mtx_count = hdr->numMatrices;
    r->shade[0] = (unsigned char) ((mr->fogcolour.word >> 24) & 0xff);
    r->shade[1] = (unsigned char) ((mr->fogcolour.word >> 16) & 0xff);
    r->shade[2] = (unsigned char) ((mr->fogcolour.word >> 8) & 0xff);
    r->shade[3] = (unsigned char) (mr->fogcolour.word & 0xff);
    r->fade = 255;
    r->passes = (unsigned char) passes;
    gdl->words.w0 = (u32) SL_CHR_DL_W0;
    gdl->words.w1 = (u32) r;
    mr->gdl = gdl + 1;
    g_emitted++;
    g_pctx.record = r;
    return r;
}

/* THE WORN HAT. The game draws a character's hat inside the character's own
 * walk: chr.c places the hat model on the head (instcalcmatrices from the
 * head's node, with the game's per-head offsets) and the joint list draws it
 * after the body. An item for the hat's model (class PROP, prop/<model>,
 * related to it by sl_prop_derive like any one-piece prop) rides the hat
 * model's matrix, and every list of that model is withheld; on a head item,
 * without the per-head entry (hat_seat). A shot-off hat is a free object:
 * it falls as an object and the object render draws it. */
static void hat_begin(ChrRecord *chr, ModelRenderData *mr)
{
    PropRecord *hp = chr->handle_positiondata_hat;
    ObjectRecord *obj;
    const ModelFileHeader *hdr;
    const struct sl_chr_map *map;
    const void *asset;
    const char *file;
    char item[SL_CHR_ITEM_MAX];

    if (hp == NULL || !prop_seam_on()) return;
    obj = hp->obj;
    if (obj == NULL || obj->model == NULL || obj->model->obj == NULL
        || obj->model->render_pos == NULL || obj->obj < 0) return;
    file = PitemZ_entries[obj->obj].filename;
    if (!prop_item_of(file, item)) return;
    asset = sl_wa_lookup(SL_CHR_CLASS_PROP, item);
    if (asset == NULL || objGetDestroyedLevel(obj) != 0) return;
    hdr = obj->model->obj;
    map = prop_map(PitemZ_entries[obj->obj].header, hdr, file, asset, 0);
    if (map == NULL || map->count != 1) return;
    g_hat.rec = prop_emit(obj, hdr, asset, map, mr);
    g_hat.root = hdr->RootNode;
    g_hat.on = 1;
    g_hat.asset = asset;
    g_hat.map = map;
    g_hat.hdr = hdr;
    g_hat.file = file;
    g_hat.base = obj->model->attachedto_objinst != NULL
               ? (int) modelFindNodeMtxIndex(obj->model->attachedto_objinst, 0) : -1;
}

/* THE HELD WEAPON (src/sl_chr_asset.h, THE COLLECTABLE). chr.c places each
 * weapon the character carries in its hand (chrRenderHeldWeapon: the model's
 * matrices from the hand's node, turned for the left hand) and the joint list
 * draws it after the body, under exactly the conditions repeated here. An
 * item for the weapon's model rides that model's matrix; the walk withholds
 * every list of the model but the muzzle flash, which the game shows while
 * the carrier fires. A dropped or thrown weapon is a free object, drawn by
 * the object render (sl_chr_prop_begin). */
static void held_begin(ChrRecord *chr, ModelRenderData *mr)
{
    int hand;
    if (!prop_seam_on()) return;
    for (hand = 0; hand < 2; hand++) {
        PropRecord *wp = chrGetEquippedWeaponProp(chr, hand == 0 ? GUNRIGHT : GUNLEFT);
        ObjectRecord *obj;
        const ModelFileHeader *hdr;
        const struct sl_chr_map *map;
        const void *asset;
        const char *file;
        char item[SL_CHR_ITEM_MAX];
        if (wp == NULL || (obj = wp->obj) == NULL) continue;
        if ((obj->runtime_bitflags & (RUNTIMEBITFLAG_00000800 | RUNTIMEBITFLAG_00000080))
            || (s32) (obj->flags2 << 12) < 0) continue;     /* the game draws none */
        if (obj->model == NULL || obj->model->obj == NULL || obj->model->render_pos == NULL
            || obj->obj < 0 || (int) (u8) obj->type != SL_PROP_KIND_COLLECTABLE) continue;
        file = PitemZ_entries[obj->obj].filename;
        if (!prop_item_of(file, item)) continue;
        asset = sl_wa_lookup(SL_CHR_CLASS_PROP, item);
        if (asset == NULL || objGetDestroyedLevel(obj) != 0) continue;
        hdr = obj->model->obj;
        map = prop_map(PitemZ_entries[obj->obj].header, hdr, file, asset, 1);
        if (map == NULL || map->count != 1) continue;
        (void) prop_emit(obj, hdr, asset, map, mr);
        g_held[hand].on = 1;
        g_held[hand].root = hdr->RootNode;
        g_held[hand].flash[0] = gunfire_switch(hdr, SL_PROP_GUNFIRE_A);
        g_held[hand].flash[1] = gunfire_switch(hdr, SL_PROP_GUNFIRE_B);
    }
}

/* THE HAT'S REST, one per (head item, hat item) pair: sl_hat_rest over the
 * two items in the hat node's frame, computed once. */
#define SL_HAT_RESTS 128
static struct {
    const void *head, *hat;
    float rest;
} g_rests[SL_HAT_RESTS];
static int g_nrests, g_rest_at;

static float hat_rest(const void *head, const struct sl_chr_map *head_map, int headnum)
{
    struct sl_prop_box b;
    int i, j = -1;
    float rest = 0.0f;
    for (i = 0; i < g_nrests; i++)
        if (g_rests[i].head == head && g_rests[i].hat == g_hat.asset) return g_rests[i].rest;
    if (g_hat.base >= 0 && head_map != NULL && g_hat.map != NULL && prop_box(g_hat.hdr, &b)) {
        for (j = 0; j < head_map->count; j++)
            if (head_map->mtx[j] == g_hat.base && head_map->rot[j] == 0) break;
        if (j < head_map->count)
            rest = sl_hat_rest(head, head_map->bind[j], head_map->scale,
                               g_hat.asset, g_hat.map->bind[0], g_hat.map->scale, b.root_origin);
        else
            j = -1;
    }
    i = g_rest_at++ % SL_HAT_RESTS;               /* a ring: once a pair */
    g_rests[i].head = head;
    g_rests[i].hat = g_hat.asset;
    g_rests[i].rest = rest;
    if (g_nrests < SL_HAT_RESTS) g_nrests++;
    fprintf(stderr, "sl_propasset: worn hat %s rests %.2f above its node on head %d's item "
                    "(head joint %d on matrix %d)\n",
            g_hat.file, (double) rest, headnum, j, g_hat.base);
    return rest;
}

/* THE WORN HAT'S SEAT (src/sl_chr_asset.h, sl_hat_unseat): chr.c moved and
 * sized the hat by its per-head entry, fitted to the cartridge's head. When
 * the head drawn under the hat item is the presentation's own (`head`, with
 * its map), the entry the game read this frame is recorded so the renderer
 * takes it back, and the hat rests on that head (sl_hat_rest); on the
 * game's head the hat item keeps the entry. The condition is chr.c's. */
static void hat_seat(ChrRecord *chr, const void *head, const struct sl_chr_map *head_map)
{
    const struct headHat *e;
    int hat;
    if (g_hat.rec == NULL || head == NULL) return;
    if (chr->headnum < HEAD_START || chr->headnum >= BODY_Female_Sally) return;
    hat = (int) get_hat_model(chr->handle_positiondata_hat);
    if (hat < 0 || hat >= 6) return;
    e = &headHat_array_8003E464[(chr->headnum - HEAD_START) * 6 + hat];
    g_hat.rec->seat[0] = e->xoffset;
    g_hat.rec->seat[1] = e->yoffset;
    g_hat.rec->seat[2] = e->zoffset;
    g_hat.rec->seat[3] = e->xsize;
    g_hat.rec->seat[4] = e->ysize;
    g_hat.rec->seat[5] = e->zsize;
    g_hat.rec->rest = hat_rest(head, head_map, (int) chr->headnum);
}

/* SL_DYNDOOR=0 is D-027's switch: the dynamic-door contract (src/sl_chr_asset.h,
 * THE DYNAMIC DOOR) is off and every door draws as the cartridge draws it,
 * under every presentation. */
static int dyndoor_on(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_DYNDOOR");
                  on = (v == NULL || *v != '0'); }
    return on;
}

/* THE RIGID DOOR (src/sl_chr_asset.h): a door the game moves only under its
 * matrix. An item for its model rides that matrix whole, as a rigid prop's
 * does - no cut, every list of the model withheld; no item, and the game's
 * lists draw untouched. THE PARTED DOOR (`parts`): an eye or iris door, whose
 * leaves the object render turns under matrices of their own; its item rides
 * those matrices by its joints (sl_prop_derive's moving prop), and an item of
 * one rigid piece is refused. */
static void door_ride(PropRecord *prop, ModelRenderData *mr, int parts)
{
    ObjectRecord *obj = prop->obj;
    const ModelFileHeader *hdr;
    const struct sl_chr_map *map;
    const void *asset;
    const char *file;
    char item[SL_CHR_ITEM_MAX];

    if (!seam_on() || !prop_seam_on() || !sl_wa_active()) return;
    if (obj->model == NULL || obj->model->obj == NULL || obj->model->render_pos == NULL) return;
    if (mr->unk18 != 0) return;                   /* a sorting walk emits nothing */
    if (obj->obj < 0) return;
    file = PitemZ_entries[obj->obj].filename;
    if (!prop_item_of(file, item)) return;
    hdr = obj->model->obj;
    g_pctx.watch = pres_on();
    g_pctx.file = file;
    g_pctx.type = SL_PROP_KIND_DOOR;
    g_pctx.state = PART_MISS;
    g_pctx.root = hdr->RootNode;
    asset = sl_wa_lookup(SL_CHR_CLASS_PROP, item);
    if (asset == NULL) return;
    if (objGetDestroyedLevel(obj) != 0) { g_pctx.state = PART_HELD; return; }
    map = prop_map(PitemZ_entries[obj->obj].header, hdr, file, asset, 0);
    if (map == NULL || (parts ? map->count < 2 : map->count != 1)) {
        g_pctx.state = PART_REFUSED;
        return;
    }
    (void) prop_emit(obj, hdr, asset, map, mr);
    g_pctx.state = PART_REPLACED;
    g_pctx.active = 1;                           /* every list of the model withheld */
}

/* THE DYNAMIC DOOR. The cut is the game's live box, read here, at the draw,
 * every frame; nothing about the door is kept between frames. */
static void door_begin(PropRecord *prop, ModelRenderData *mr)
{
    ObjectRecord *obj = prop->obj;
    DoorRecord *door = (DoorRecord *) obj;
    const ModelFileHeader *hdr;
    const struct sl_chr_map *map = NULL;
    const void *asset = NULL;
    const char *file = NULL;
    char item[SL_CHR_ITEM_MAX];
    struct sl_door_cut cut;
    struct sl_chr_draw *r;
    float box[6];
    int ready = 0;

    if (sl_door_path(dyndoor_on(), sl_door_takes((int) door->doorFlags, (int) door->doorType), 0)
        == SL_DOOR_PATH_GAME) {
        if (dyndoor_on() && sl_door_rides((int) door->doorFlags, (int) door->doorType))
            door_ride(prop, mr, 0);
        else if (dyndoor_on() && sl_door_parts((int) door->doorFlags, (int) door->doorType))
            door_ride(prop, mr, 1);
        return;
    }
    if (obj->model == NULL || obj->model->obj == NULL || obj->model->render_pos == NULL) return;
    if (mr->unk18 != 0) return;                   /* a sorting walk emits nothing */
    box[0] = door->bbox.Bounds.xmin; box[1] = door->bbox.Bounds.xmax;
    box[2] = door->bbox.Bounds.ymin; box[3] = door->bbox.Bounds.ymax;
    box[4] = door->bbox.Bounds.zmin; box[5] = door->bbox.Bounds.zmax;
    if (!sl_door_cut_of((int) door->doorType, box, &cut)) return;
    hdr = obj->model->obj;
    if (obj->obj >= 0) file = PitemZ_entries[obj->obj].filename;
    g_pctx.watch = pres_on();
    g_pctx.file = file;
    g_pctx.type = SL_PROP_KIND_DOOR;
    g_pctx.state = PART_MISS;
    g_pctx.root = hdr->RootNode;
    if (seam_on() && prop_seam_on() && sl_wa_active() && prop_item_of(file, item))
        asset = sl_wa_lookup(SL_CHR_CLASS_PROP, item);
    if (asset != NULL) {
        if (objGetDestroyedLevel(obj) != 0) {
            g_pctx.state = PART_HELD;
        } else if (!sl_door_item_fits(asset)) {
            g_pctx.state = PART_REFUSED;
        } else {
            map = prop_map(PitemZ_entries[obj->obj].header, hdr, file, asset, 0);
            if (map == NULL || map->count != 1) g_pctx.state = PART_REFUSED;
            else ready = 1;
        }
    }
    if (sl_door_path(1, 1, ready) == SL_DOOR_PATH_ITEM) {
        r = prop_emit(obj, hdr, asset, map, mr);
        if (r != NULL) r->cut = cut;
        g_pctx.state = PART_REPLACED;
        g_pctx.active = 1;                       /* every list of the model withheld */
        return;
    }
    /* No item: the game's own lists, reshaped by the game, kept closed at
     * the eye by the renderer between the two markers. */
    if (mr->flags & 1) {
        Gfx *gdl = mr->gdl;
        gdl->words.w0 = (u32) SL_DOOR_DL_W0;
        gdl->words.w1 = 1u;
        mr->gdl = gdl + 1;
        g_pctx.door_open = 1;
    }
}

/* THE DESTROYED AUTOGUN (src/sl_chr_asset.h): the matrix the model's first
 * display list is drawn under - the list objDeform copies and blackens
 * (propobj.c, sub_GAME_7F04B478: the first DLCOLLISION node in tree order;
 * read here without the relations that walk applies). -1 when none. */
static int deformed_mtx(const ModelFileHeader *hdr)
{
    const ModelNode *nd = hdr->RootNode;
    int guard = 0;
    while (nd != NULL && guard++ < 4096) {
        if ((nd->Opcode & 0xff) == MODELNODE_OPCODE_DLCOLLISION && nd->Data != NULL)
            return (int) modelFindNodeMtxIndex((ModelNode *) nd, 0);
        if (nd->Child != NULL) { nd = nd->Child; continue; }
        while (nd != NULL && nd != hdr->RootNode && nd->Next == NULL) nd = nd->Parent;
        if (nd == NULL || nd == hdr->RootNode) break;
        nd = nd->Next;
    }
    return -1;
}

void sl_chr_prop_begin(PropRecord *prop, ModelRenderData *mr)
{
    ObjectRecord *obj;
    const ModelFileHeader *hdr;
    const struct sl_chr_map *map;
    const void *asset;
    const char *file;
    struct sl_chr_draw *r;
    char item[SL_CHR_ITEM_MAX];
    int type, screens, k, level;

    memset(&g_pctx, 0, sizeof g_pctx);
    if (prop == NULL || mr == NULL || prop->obj == NULL) return;
    if ((int) (u8) prop->obj->type == SL_PROP_KIND_DOOR) { door_begin(prop, mr); return; }
    if (!seam_on() || !prop_seam_on() || !sl_wa_active()) return;
    obj = prop->obj;
    if (obj->model == NULL || obj->model->obj == NULL || obj->model->render_pos == NULL) return;
    if (mr->unk18 != 0) return;                   /* a sorting walk emits nothing */
    type = (int) (u8) obj->type;
    screens = sl_prop_takes(type, obj->model->obj->numSwitches);
    if (screens < 0) return;
    if (obj->obj < 0) return;
    file = PitemZ_entries[obj->obj].filename;
    if (!prop_item_of(file, item)) return;
    hdr = obj->model->obj;
    g_pctx.watch = pres_on();
    g_pctx.file = file;
    g_pctx.type = type;
    g_pctx.state = PART_MISS;
    g_pctx.root = hdr->RootNode;
    asset = sl_wa_lookup(SL_CHR_CLASS_PROP, item);
    if (asset == NULL) return;
    /* THE DESTROYED AUTOGUN keeps its item and takes the game's level; a
     * destroyed object of any other kind draws as the game's */
    level = (int) objGetDestroyedLevel(obj);
    g_pctx.destroyed = level;
    g_pctx.destroyed_mtx = (level != 0) ? deformed_mtx(hdr) : -1;
    if (level != 0 && !sl_prop_destroyed_keeps(type)) { g_pctx.state = PART_HELD; return; }
    map = prop_map(PitemZ_entries[obj->obj].header, hdr, file, asset,
                   type == SL_PROP_KIND_KEY || type == SL_PROP_KIND_COLLECTABLE);
    if (map == NULL) { g_pctx.state = PART_REFUSED; return; }

    r = prop_emit(obj, hdr, asset, map, mr);
    if (r != NULL && level != 0) {
        r->destroyed = (unsigned char) (level > 255 ? 255 : level);
        r->destroyed_mtx = deformed_mtx(hdr);
    }
    g_pctx.state = PART_REPLACED;
    g_pctx.active = 1;
    if (sl_prop_keeps_gunfire(type)) {
        g_pctx.keep[0] = gunfire_switch(hdr, SL_PROP_GUNFIRE_A);
        g_pctx.keep[1] = gunfire_switch(hdr, SL_PROP_GUNFIRE_B);
    }
    for (k = 0; k < screens && k < SL_PROP_MAX_SCREENS; k++) {
        g_pctx.keep[k] = hdr->Switches[k];
        if (screen_place_on())
            screen_place(obj, hdr, asset, map, k, hdr->Switches[k], file);
    }
}

static void prop_report(void)
{
    static const char *const nm[] = { "-", "MISS", "REFUSED", "DAMAGED", "REPLACED", "-" };
    static struct { const char *file; unsigned key; } last[64];
    static unsigned lines;
    unsigned key = (((unsigned) g_pctx.state * 131u + g_pctx.kept) * 131u + g_pctx.withheld) * 131u
                   + (unsigned) g_pctx.destroyed;
    int i, at = -1;
    for (i = 0; i < 64; i++)
        if (last[i].file == g_pctx.file) { at = i; break; }
    if (at < 0)
        for (i = 0; i < 64; i++)
            if (last[i].file == NULL) { at = i; break; }
    if (at < 0 || (last[at].file == g_pctx.file && last[at].key == key)) return;
    last[at].file = g_pctx.file;
    last[at].key = key;
    if (lines++ >= 256) return;
    fprintf(stderr, "sl_proppres: seam prop=%s type=%d state=%s bridge=%p native lists "
                    "kept=%u withheld=%u destroyed=%d deformed-mtx=%d\n", g_pctx.file ? g_pctx.file : "-",
            g_pctx.type, nm[g_pctx.state], g_pctx.record, g_pctx.kept, g_pctx.withheld, g_pctx.destroyed,
            g_pctx.destroyed_mtx);
}

void sl_chr_prop_end(ModelRenderData *mr)
{
    /* THE DYNAMIC DOOR: close the bracket its lists were drawn in */
    if (g_pctx.door_open && mr != NULL && (mr->flags & 1)) {
        Gfx *gdl = mr->gdl;
        gdl->words.w0 = (u32) SL_DOOR_DL_W0;
        gdl->words.w1 = 0u;
        mr->gdl = gdl + 1;
    }
    if (g_pctx.watch) prop_report();
    g_pctx.active = 0;
    g_pctx.watch = 0;
    g_pctx.door_open = 0;
}

static int prop_skip(const ModelNode *node)
{
    const ModelNode *n, *top = node;
    int guard = 0;
    for (n = node; n != NULL && guard++ < 64; n = n->Parent) {
        int s;
        for (s = 0; s < SL_PROP_MAX_SCREENS; s++)
            if (g_pctx.keep[s] != NULL && n == g_pctx.keep[s]) { g_pctx.kept++; return 0; }
        top = n;
    }
    if (top != g_pctx.root) return 0;
    if (!g_pctx.active) { g_pctx.kept++; return 0; }
    g_pctx.withheld++;
    g_withheld++;
    return 1;
}

/* -1 when no prop is being drawn; else prop_skip's answer. */
static int prop_skip_any(const void *nodev)
{
    if (!(g_pctx.active || g_pctx.watch) || nodev == NULL) return -1;
    return prop_skip((const ModelNode *) nodev);
}

/* The census line the renderer prints with its own. */
void sl_chr_asset_report(void *f)
{
    fprintf(f, "sl_chrasset: bridge draws=%u withheld native lists=%u maps=%d trees=%d\n",
            g_emitted, g_withheld, g_nmaps, g_ntrees);
}

/* For the crash report (src/native/sl_crash_model.c): whether this session
 * derived a map that rides the model file `key` (its header in c_item_entries
 * or PitemZ_entries) - bit 0 a character item, bit 1 a prop item. Reads the
 * caches only; safe inside the fault handler. */
unsigned sl_chr_asset_rides(const void *key)
{
    unsigned r = 0;
    int i;
    if (key == NULL) return 0;
    for (i = 0; i < g_nmaps && i < SL_CHR_MAPS; i++)
        if (g_maps[i].hdr == key && g_maps[i].ok) r |= 1u;
    for (i = 0; i < g_npmaps && i < SL_PROP_MAPS; i++)
        if (g_pmaps[i].hdr == key && g_pmaps[i].ok) r |= 2u;
    return r;
}

#endif /* __sgi */
