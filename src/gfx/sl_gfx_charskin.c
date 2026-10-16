/**
 * sl_gfx_charskin.c - see sl_gfx_charskin.h and src/sl_chr_asset.h.
 *
 * WHAT sl_chr_derive MATCHES ON, and why that is enough. A replacement item
 * names the game's joints and places them, at a uniform scale, where the game
 * places them - but in its own BIND orientation: the game lays each limb's
 * part frame along an axis (a thigh along x, its knee origin (-430.3, -7.8,
 * 0) from the hip) where the item hangs the same limb down, several times
 * longer. A joint's frame is fixed by the bones
 * that leave it, so each item joint is matched to a game joint by its child
 * bones: the quarter turn about z that carries every child bone's direction
 * onto a game child's origin direction (to within SL_CHR_DOT_MIN), and the
 * children pair off by that match. The scale is the ratio of the matched
 * bone lengths, and every bone must agree with it (SL_CHR_SCALE_TOL). A joint
 * with no child bone - a wrist, an ankle, the neck the head hangs from - has
 * no bone to fix its frame, and keeps its parent's turn: the part it carries
 * continues the limb it ends. Nothing here is a per-character table.
 *
 * Anything that does not match - a different topology, a bone no quarter turn
 * aligns, a length off the common scale, a vertex bound to a joint the game
 * does not have - is REFUSED with the reason, and the character draws as the
 * game's.
 */
#include <math.h>
#include <string.h>
#include <stdio.h>

#include "sl_gfx_charskin.h"

#if SL_CHR_CLASS_CHAR != SL_WA_CLASS_CHAR || SL_CHR_CLASS_HEAD != SL_WA_CLASS_HEAD
#error "src/sl_chr_asset.h and sl_gfx_worldasset.h disagree on the character classes"
#endif
#if SL_CHR_CLASS_PROP != SL_WA_CLASS_PROP
#error "src/sl_chr_asset.h and sl_gfx_worldasset.h disagree on the prop class"
#endif
#if SL_CHR_CLASS_GUN != SL_WA_CLASS_GUN || SL_VM_MAX_SWITCH != SL_WA_MAX_SWITCH
#error "src/sl_chr_asset.h and sl_gfx_worldasset.h disagree on the view-model class"
#endif
#if SL_VM_MAX_MTX != SL_WA_MAX_PART_MATRIX
#error "src/sl_chr_asset.h and sl_gfx_worldasset.h disagree on the model matrix limit"
#endif
#if SL_PROP_MAX_MTX != SL_WA_MAX_PART_MATRIX
#error "src/sl_chr_asset.h and sl_gfx_worldasset.h disagree on a prop's matrix limit"
#endif
#if SL_CHR_MAX_JOINTS != SL_WA_MAX_JOINTS
#error "src/sl_chr_asset.h and sl_gfx_worldasset.h disagree on the joint limit"
#endif

#define SL_CHR_DOT_MIN     0.985f   /* cos 10 degrees */
#define SL_CHR_SCALE_TOL   0.03f    /* 3% of the common scale */
#define SL_CHR_TINY        1.0e-4f
#define SL_PROP_PIVOT_TOL  0.10f    /* of the item's box extent: how far a
                                       moving part's own joint may sit from
                                       the model's node for its matrix */
#define SL_PROP_SAME_PIVOT 0.001f   /* of the item's box extent: a joint this
                                       near its parent's has no pivot of its
                                       own (sl_prop_derive) */

static void rot_apply(int k, const float v[3], float out[3])
{
    float x = v[0], y = v[1];
    switch (k & 3) {
    case 0: out[0] =  x; out[1] =  y; break;
    case 1: out[0] = -y; out[1] =  x; break;
    case 2: out[0] = -x; out[1] = -y; break;
    default: out[0] =  y; out[1] = -x; break;
    }
    out[2] = v[2];
}

static float len3(const float v[3])
{
    return (float) sqrt((double) v[0] * v[0] + (double) v[1] * v[1] + (double) v[2] * v[2]);
}

/* The cosine between two directions; two vanishing vectors agree, one
 * vanishing and one not do not. */
static float dir_dot(const float a[3], const float b[3])
{
    float la = len3(a), lb = len3(b);
    if (la < SL_CHR_TINY || lb < SL_CHR_TINY)
        return (la < SL_CHR_TINY && lb < SL_CHR_TINY) ? 1.0f : 0.0f;
    return (a[0] * b[0] + a[1] * b[1] + a[2] * b[2]) / (la * lb);
}

int sl_chr_derive(const void *asset, const struct sl_chr_tree *tree,
                  struct sl_chr_map *out, char *why)
{
    const struct sl_wa_asset *a = (const struct sl_wa_asset *) asset;
    int na, ng, j, g, hub_a = -1, hub_g = -1;
    int game_of[SL_CHR_MAX_JOINTS], rot[SL_CHR_MAX_JOINTS], done[SL_CHR_MAX_JOINTS];
    int nchild_a[SL_CHR_MAX_JOINTS], nchild_g[SL_CHR_MAX_JOINTS];
    int queue[SL_CHR_MAX_JOINTS], qh = 0, qt = 0;
    double sum_a = 0.0, sum_g = 0.0;
    float scale;
    unsigned i;

    memset(out, 0, sizeof *out);
    if (why) why[0] = '\0';
#define REFUSE(...) do { if (why) snprintf(why, 96, __VA_ARGS__); return 0; } while (0)
    if (a == NULL || tree == NULL) REFUSE("no item or no skeleton");
    na = (int) a->joint_count;
    ng = tree->count;
    if (a->skin == NULL || na <= 0 || na > SL_CHR_MAX_JOINTS)
        REFUSE("the item carries no usable skeleton");
    if (ng <= 0 || ng > SL_CHR_MAX_JOINTS) REFUSE("the model has no usable skeleton");

    for (j = 0; j < na; j++) { game_of[j] = -1; rot[j] = 0; done[j] = 0; nchild_a[j] = 0; }
    for (g = 0; g < ng; g++) nchild_g[g] = 0;
    for (j = 0; j < na; j++) if (a->joints[j].parent >= 0) nchild_a[a->joints[j].parent]++;
    for (g = 0; g < ng; g++) {
        int p = tree->parent[g];
        if (p >= g) REFUSE("the model's skeleton is not in tree order");
        if (p >= 0) nchild_g[p]++;
    }
    /* THE HUB: the first joint that branches, on each side. Everything above
     * it on the item side is the root chain (the item's position root) and
     * rides the same matrix, unturned. */
    for (j = 0; j < na && hub_a < 0; j++) if (nchild_a[j] >= 2) hub_a = j;
    for (g = 0; g < ng && hub_g < 0; g++) if (nchild_g[g] >= 2) hub_g = g;
    if (hub_a < 0 || hub_g < 0) REFUSE("no branching joint to anchor the match on");
    for (j = hub_a; j >= 0; j = a->joints[j].parent) { game_of[j] = hub_g; done[j] = 1; }
    queue[qt++] = hub_a;

    while (qh < qt) {
        int pa = queue[qh++], pg = game_of[pa], k, best_k = -1;
        int kids[SL_CHR_MAX_JOINTS], nk = 0, gk[SL_CHR_MAX_JOINTS], ngk = 0;
        int assign[SL_CHR_MAX_JOINTS], best_assign[SL_CHR_MAX_JOINTS];
        float best_score = -1.0e30f;

        for (j = 0; j < na; j++) if (a->joints[j].parent == pa) kids[nk++] = j;
        for (g = 0; g < ng; g++) if (tree->parent[g] == pg) gk[ngk++] = g;
        if (nk == 0) continue;
        if (nk > ngk) REFUSE("%s has more child joints than the model's", a->joints[pa].name);
        for (k = 0; k < 4; k++) {
            int used[SL_CHR_MAX_JOINTS], c, ok = 1;
            float score = 0.0f;
            memset(used, 0, sizeof used);
            for (c = 0; c < nk && ok; c++) {
                float bone[3], turned[3], best = -2.0f;
                int m, pick = -1;
                for (m = 0; m < 3; m++)
                    bone[m] = a->joints[kids[c]].bind[m] - a->joints[pa].bind[m];
                rot_apply(k, bone, turned);
                for (m = 0; m < ngk; m++) {
                    float d;
                    if (used[m]) continue;
                    d = dir_dot(turned, tree->origin[gk[m]]);
                    if (d > best) { best = d; pick = m; }
                }
                if (pick < 0 || best < SL_CHR_DOT_MIN) { ok = 0; break; }
                used[pick] = 1;
                assign[c] = gk[pick];
                score += best;
            }
            if (ok && score > best_score) {
                best_score = score; best_k = k;
                memcpy(best_assign, assign, sizeof assign);
            }
        }
        if (best_k < 0) REFUSE("no quarter turn aligns the bones below %s", a->joints[pa].name);
        rot[pa] = best_k;
        for (j = 0; j < nk; j++) {
            int c = kids[j];
            float bone[3];
            int m;
            game_of[c] = best_assign[j];
            done[c] = 1;
            for (m = 0; m < 3; m++) bone[m] = a->joints[c].bind[m] - a->joints[pa].bind[m];
            sum_a += len3(bone);
            sum_g += len3(tree->origin[game_of[c]]);
            queue[qt++] = c;
        }
    }
    if (!(sum_a > 0.0) || !(sum_g > 0.0)) REFUSE("no bone length to take a scale from");
    scale = (float) (sum_a / sum_g);
    /* Every matched bone on the one scale; a leaf keeps its parent's turn. */
    for (j = 0; j < na; j++) {
        int p = a->joints[j].parent;
        if (!done[j] || p < 0 || game_of[p] < 0 || j == hub_a) continue;
        if (game_of[j] != game_of[p]) {
            float bone[3], la, lg;
            int m;
            for (m = 0; m < 3; m++) bone[m] = a->joints[j].bind[m] - a->joints[p].bind[m];
            la = len3(bone);
            lg = len3(tree->origin[game_of[j]]);
            if (lg > SL_CHR_TINY && fabs((double) (la / lg - scale)) > SL_CHR_SCALE_TOL * scale)
                REFUSE("the bone to %s is off the common scale", a->joints[j].name);
        }
        if (nchild_a[j] == 0) rot[j] = rot[p];
    }

    out->count = na;
    out->scale = scale;
    for (j = 0; j < na; j++) {
        out->mtx[j] = (game_of[j] >= 0) ? tree->mtx[game_of[j]] : -1;
        out->rot[j] = rot[j];
        memcpy(out->bind[j], a->joints[j].bind, sizeof out->bind[j]);
    }
    for (i = 0; i < a->vertex_count; i++) {
        const struct sl_wa_skin *s = &a->skin[i];
        unsigned l;
        for (l = 0; l < SL_WA_SKIN_LANES; l++)
            if (s->weight[l] != 0 && out->mtx[s->joint[l]] < 0)
                REFUSE("a vertex is bound to %s, which the model does not have",
                       a->joints[s->joint[l]].name);
    }
#undef REFUSE
    out->ok = 1;
    return 1;
}

/* THE VIEW MODEL (src/sl_chr_asset.h). A weapon item is authored in the
 * game's own view-model frame, a uniform scale larger: its joints ARE the
 * game's parts by name (the preparation resolved each name to the Switches[]
 * node the game positions that part with), and no limb is re-laid, so the
 * relation is the scale alone. It is measured, not assumed - the ratio of the
 * matched joints' distances from the root, bind for model - and every joint
 * must then sit where the game's part sits (SL_CHR_SCALE_TOL of its distance
 * from the root, the root itself included), which is also what refuses an
 * item turned against the game's frame. A joint that is no part the game
 * positions takes no matrix, and a vertex bound to one refuses the item. */
/* THE WATCH HAND (src/sl_chr_asset.h): an item whose joints name the model's
 * MATRICES - the arm's wrist and the watch's hands, which the game positions
 * by its GROUP tree. The arm item is authored hanging where the game lays
 * the forearm along an axis, so the relation carries a quarter turn about z
 * as a character's does; unlike a character it is read from the named joints
 * alone (an arm has no hub to match bones at): the first named joint is the
 * anchor, the scale the ratio of the others' distances from it, and the turn
 * the one quarter turn under which every named joint sits on its node. */
static int vm_derive_arm(const struct sl_wa_asset *a, const struct sl_vm_parts *parts,
                         struct sl_chr_map *out, char *why)
{
    int gm[SL_CHR_MAX_JOINTS], anchor = -1, named = 0, turn = -1, j, k, m;
    double sum_a = 0.0, sum_g = 0.0;
    float scale;
    unsigned i;
#define REFUSE(...) do { if (why) snprintf(why, 96, __VA_ARGS__); return 0; } while (0)
    for (j = 0; j < (int) a->joint_count; j++) {
        unsigned part = a->joints[j].part;
        gm[j] = -1;
        if (part == 0u) continue;
        if (part <= SL_WA_PART_MATRIX)
            REFUSE("%s names a Switches[] part beside the model's matrices", a->joints[j].name);
        m = (int) (part - SL_WA_PART_MATRIX - 1u);
        if (m >= parts->mtx_count || m >= SL_VM_MAX_MTX || !parts->node_present[m])
            REFUSE("%s names matrix %d, under which the model positions no part",
                   a->joints[j].name, m);
        gm[j] = m;
        if (anchor < 0) anchor = j;
        named++;
    }
    if (named < 2) REFUSE("fewer than two joints name the model's matrices");
    for (j = 0; j < (int) a->joint_count; j++) {
        float da[3], dg[3];
        if (j == anchor || gm[j] < 0) continue;
        for (m = 0; m < 3; m++) {
            da[m] = a->joints[j].bind[m] - a->joints[anchor].bind[m];
            dg[m] = parts->node_origin[gm[j]][m] - parts->node_origin[gm[anchor]][m];
        }
        sum_a += len3(da);
        sum_g += len3(dg);
    }
    if (!(sum_a > 0.0) || !(sum_g > 0.0)) REFUSE("the named joints do not spread");
    scale = (float) (sum_a / sum_g);
    for (k = 0; k < 4; k++) {
        int fits = 1;
        for (j = 0; j < (int) a->joint_count && fits; j++) {
            float da[3], dg[3], r[3], d[3];
            if (j == anchor || gm[j] < 0) continue;
            for (m = 0; m < 3; m++) {
                da[m] = (a->joints[j].bind[m] - a->joints[anchor].bind[m]) / scale;
                dg[m] = parts->node_origin[gm[j]][m] - parts->node_origin[gm[anchor]][m];
            }
            rot_apply(k, da, r);
            for (m = 0; m < 3; m++) d[m] = r[m] - dg[m];
            if (len3(d) > SL_CHR_SCALE_TOL * len3(dg) + 1.0f) fits = 0;
        }
        if (!fits) continue;
        if (turn >= 0) REFUSE("more than one quarter turn sets the joints on the model's nodes");
        turn = k;
    }
    if (turn < 0) REFUSE("no quarter turn about z sets the joints on the model's nodes");
    out->count = (int) a->joint_count;
    out->scale = scale;
    for (j = 0; j < (int) a->joint_count; j++) {
        out->mtx[j] = gm[j];
        out->rot[j] = turn;
        memcpy(out->bind[j], a->joints[j].bind, sizeof out->bind[j]);
    }
    for (i = 0; i < a->vertex_count; i++) {
        const struct sl_wa_skin *s = &a->skin[i];
        unsigned l;
        for (l = 0; l < SL_WA_SKIN_LANES; l++)
            if (s->weight[l] != 0 && out->mtx[s->joint[l]] < 0)
                REFUSE("a vertex is bound to %s, which names none of the model's matrices",
                       a->joints[s->joint[l]].name);
    }
#undef REFUSE
    out->ok = 1;
    return 1;
}

int sl_vm_derive(const void *asset, const struct sl_vm_parts *parts,
                 struct sl_chr_map *out, char *why)
{
    const struct sl_wa_asset *a = (const struct sl_wa_asset *) asset;
    float go[SL_CHR_MAX_JOINTS][3];
    int gm[SL_CHR_MAX_JOINTS], root = -1, j, m;
    double sum_a = 0.0, sum_g = 0.0;
    float scale;
    unsigned i;

    memset(out, 0, sizeof *out);
    if (why) why[0] = '\0';
#define REFUSE(...) do { if (why) snprintf(why, 96, __VA_ARGS__); return 0; } while (0)
    if (a == NULL || parts == NULL) REFUSE("no item or no view model");
    if (a->asset_class != SL_WA_CLASS_GUN) REFUSE("the item is not a view model");
    if (a->skin == NULL || a->joint_count == 0u || a->joint_count > SL_CHR_MAX_JOINTS)
        REFUSE("the item carries no usable skeleton");
    for (j = 0; j < (int) a->joint_count; j++)
        if (a->joints[j].part > SL_WA_PART_MATRIX) return vm_derive_arm(a, parts, out, why);
    if (parts->root_mtx < 0) REFUSE("the model has no root part for the item's root");
    for (j = 0; j < (int) a->joint_count; j++) {
        const struct sl_wa_joint *jt = &a->joints[j];
        gm[j] = -1;
        if (jt->parent < 0) {
            if (root >= 0) REFUSE("the item has two root joints");
            root = j;
            memcpy(go[j], parts->root_origin, sizeof go[j]);
            gm[j] = parts->root_mtx;
        } else if (jt->part != 0u) {
            int k = (int) jt->part - 1;
            if (k >= parts->count || !parts->present[k])
                REFUSE("%s names a part this model does not position", jt->name);
            memcpy(go[j], parts->origin[k], sizeof go[j]);
            gm[j] = parts->mtx[k];
        }
        if (gm[j] >= parts->mtx_count) REFUSE("%s rides a matrix the model does not have", jt->name);
    }
    if (root < 0) REFUSE("the item has no root joint");
    for (j = 0; j < (int) a->joint_count; j++) {
        float da[3], dg[3];
        if (j == root || gm[j] < 0) continue;
        for (m = 0; m < 3; m++) {
            da[m] = a->joints[j].bind[m] - a->joints[root].bind[m];
            dg[m] = go[j][m] - go[root][m];
        }
        sum_a += len3(da);
        sum_g += len3(dg);
    }
    /* An item whose only positioned joint is its root - a fist, a knife: the
     * model moves no part of it on its own - is related by the root alone,
     * its bind against the model root's origin (both measured from the
     * model's origin); the position check below still refuses one that
     * points elsewhere. */
    if (!(sum_a > 0.0) && !(sum_g > 0.0)) {
        sum_a = len3(a->joints[root].bind);
        sum_g = len3(go[root]);
    }
    if (!(sum_a > 0.0) || !(sum_g > 0.0)) REFUSE("no part distance to take a scale from");
    scale = (float) (sum_a / sum_g);
    for (j = 0; j < (int) a->joint_count; j++) {
        float d[3], ref;
        if (gm[j] < 0) continue;
        for (m = 0; m < 3; m++)
            d[m] = (j == root) ? a->joints[j].bind[m] - scale * go[j][m]
                 : (a->joints[j].bind[m] - a->joints[root].bind[m]) - scale * (go[j][m] - go[root][m]);
        if (j == root) ref = len3(go[j]);
        else { float dg[3]; for (m = 0; m < 3; m++) dg[m] = go[j][m] - go[root][m]; ref = len3(dg); }
        if (len3(d) > SL_CHR_SCALE_TOL * scale * ref + 1.0f)
            REFUSE("%s does not sit where the model's part sits", a->joints[j].name);
    }
    out->count = (int) a->joint_count;
    out->scale = scale;
    for (j = 0; j < (int) a->joint_count; j++) {
        out->mtx[j] = gm[j];
        out->rot[j] = 0;
        memcpy(out->bind[j], a->joints[j].bind, sizeof out->bind[j]);
    }
    for (i = 0; i < a->vertex_count; i++) {
        const struct sl_wa_skin *s = &a->skin[i];
        unsigned l;
        for (l = 0; l < SL_WA_SKIN_LANES; l++)
            if (s->weight[l] != 0 && out->mtx[s->joint[l]] < 0)
                REFUSE("a vertex is bound to %s, which is no part the model positions",
                       a->joints[s->joint[l]].name);
    }
#undef REFUSE
    out->ok = 1;
    return 1;
}

void sl_vm_menu_look(int prop_type, unsigned envcolour, unsigned fogcolour,
                     unsigned char shade[4], int *fade)
{
    unsigned w = (prop_type == SL_VM_LOOK_TRANSLUCENT_TYPE) ? fogcolour : envcolour;
    shade[0] = (unsigned char) ((w >> 24) & 0xffu);
    shade[1] = (unsigned char) ((w >> 16) & 0xffu);
    shade[2] = (unsigned char) ((w >> 8) & 0xffu);
    shade[3] = (unsigned char) (w & 0xffu);
    *fade = (prop_type == SL_VM_LOOK_TRANSLUCENT_TYPE) ? (int) (envcolour & 0xffu) : 255;
}

int sl_prop_takes(int kind, int num_switches)
{
    switch (kind) {
    case SL_PROP_KIND_PROP:
    case SL_PROP_KIND_VEHICLE:
    case SL_PROP_KIND_GAS:
    case SL_PROP_KIND_HAT:
    case SL_PROP_KIND_ARMOUR:
    case SL_PROP_KIND_GLASS:
    case SL_PROP_KIND_AMMO:
    case SL_PROP_KIND_AUTOGUN:
    case SL_PROP_KIND_AIRCRAFT:
    case SL_PROP_KIND_TANK:
    case SL_PROP_KIND_KEY:
    case SL_PROP_KIND_ALARM:
    case SL_PROP_KIND_COLLECTABLE:
        return 0;
    case SL_PROP_KIND_MONITOR:
        return num_switches > 0 ? 1 : 0;
    case SL_PROP_KIND_MULTI_MONITOR:
        if (num_switches <= 0) return 0;
        return num_switches < SL_PROP_MAX_SCREENS ? num_switches : SL_PROP_MAX_SCREENS;
    default:
        return -1;
    }
}

int sl_prop_keeps_gunfire(int kind)
{
    return kind == SL_PROP_KIND_COLLECTABLE;
}

int sl_hat_unseat(float m[16], const float seat[6], float rest)
{
    float t[3];
    int i, c;
    for (i = 0; i < 3; i++)
        if (!(seat[3 + i] > 0.0f)) return 0;
    /* m = M x B, M the entry's rows (size on the diagonal, 21.3 x offset in
     * the last); B = M^-1 x m */
    for (i = 0; i < 3; i++) t[i] = SL_HAT_SEAT_UNIT * seat[i] / seat[3 + i];
    for (c = 0; c < 4; c++)
        m[12 + c] -= t[0] * m[c] + t[1] * m[4 + c] + t[2] * m[8 + c];
    for (i = 0; i < 3; i++)
        for (c = 0; c < 4; c++) m[4 * i + c] /= seat[3 + i];
    /* then T(0, rest, 0) ahead of the node's matrix */
    for (c = 0; c < 4; c++) m[12 + c] += rest * m[4 + c];
    return 1;
}

/* THE HAT'S REST: two top-surface height maps over x/z in the hat node's
 * frame, one game unit a cell, and a histogram of their difference. */
#define HAT_CELLS   192                  /* +-96 game units about the node */
#define HAT_BINS    1600                 /* 0.25-unit bins over +-200 units */
static float g_hat_top[2][HAT_CELLS * HAT_CELLS];
static unsigned g_hat_hist[HAT_BINS];

static float hat_min3(float a, float b, float c)
{
    float m = a < b ? a : b;
    return m < c ? m : c;
}

static float hat_max3(float a, float b, float c)
{
    float m = a > b ? a : b;
    return m > c ? m : c;
}

static void hat_raster(const struct sl_wa_asset *a, const float bind[3], float scale,
                       const float origin[3], float *top)
{
    unsigned t;
    float inv = 1.0f / scale;
    for (t = 0; t + 2 < a->index_count; t += 3) {
        float p[3][3], area;
        int k, x0, x1, z0, z1, x, z;
        for (k = 0; k < 3; k++) {
            unsigned vi = a->indices[t + (unsigned) k];
            const struct sl_wa_vertex *v;
            if (vi >= a->vertex_count) return;
            v = &a->vertices[vi];
            p[k][0] = (v->x - bind[0]) * inv + origin[0] + 0.5f * HAT_CELLS;
            p[k][1] = (v->y - bind[1]) * inv + origin[1];
            p[k][2] = (v->z - bind[2]) * inv + origin[2] + 0.5f * HAT_CELLS;
        }
        area = (p[1][0] - p[0][0]) * (p[2][2] - p[0][2]) - (p[2][0] - p[0][0]) * (p[1][2] - p[0][2]);
        if (fabsf(area) < 1e-6f) continue;          /* edge-on from above */
        x0 = (int) floorf(hat_min3(p[0][0], p[1][0], p[2][0]));
        x1 = (int) floorf(hat_max3(p[0][0], p[1][0], p[2][0]));
        z0 = (int) floorf(hat_min3(p[0][2], p[1][2], p[2][2]));
        z1 = (int) floorf(hat_max3(p[0][2], p[1][2], p[2][2]));
        if (x0 < 0) x0 = 0;
        if (z0 < 0) z0 = 0;
        if (x1 > HAT_CELLS - 1) x1 = HAT_CELLS - 1;
        if (z1 > HAT_CELLS - 1) z1 = HAT_CELLS - 1;
        for (z = z0; z <= z1; z++)
            for (x = x0; x <= x1; x++) {
                float cx = (float) x + 0.5f, cz = (float) z + 0.5f, b1, b2, y;
                b1 = ((cx - p[0][0]) * (p[2][2] - p[0][2]) - (p[2][0] - p[0][0]) * (cz - p[0][2])) / area;
                b2 = ((p[1][0] - p[0][0]) * (cz - p[0][2]) - (cx - p[0][0]) * (p[1][2] - p[0][2])) / area;
                if (b1 < 0.0f || b2 < 0.0f || b1 + b2 > 1.0f) continue;
                y = p[0][1] + b1 * (p[1][1] - p[0][1]) + b2 * (p[2][1] - p[0][1]);
                if (y > top[z * HAT_CELLS + x]) top[z * HAT_CELLS + x] = y;
            }
    }
}

float sl_hat_rest(const void *headv, const float head_bind[3], float head_scale,
                  const void *hatv, const float hat_bind[3], float hat_scale,
                  const float hat_origin[3])
{
    const struct sl_wa_asset *head = (const struct sl_wa_asset *) headv;
    const struct sl_wa_asset *hat = (const struct sl_wa_asset *) hatv;
    static const float zero[3] = { 0.0f, 0.0f, 0.0f };
    unsigned n = 0, want, run = 0;
    int i;
    if (head == NULL || hat == NULL || head->vertices == NULL || hat->vertices == NULL
        || head->indices == NULL || hat->indices == NULL
        || !(head_scale > 0.0f) || !(hat_scale > 0.0f)) return 0.0f;
    for (i = 0; i < HAT_CELLS * HAT_CELLS; i++) g_hat_top[0][i] = g_hat_top[1][i] = -1e30f;
    hat_raster(head, head_bind, head_scale, zero, g_hat_top[0]);
    hat_raster(hat, hat_bind, hat_scale, hat_origin, g_hat_top[1]);
    memset(g_hat_hist, 0, sizeof g_hat_hist);
    for (i = 0; i < HAT_CELLS * HAT_CELLS; i++) {
        float d;
        int b;
        if (g_hat_top[0][i] < -1e29f || g_hat_top[1][i] < -1e29f) continue;
        d = g_hat_top[0][i] - g_hat_top[1][i];        /* the head above the cap */
        b = (int) floorf((d + 200.0f) * 4.0f);
        if (b < 0) b = 0;
        if (b > HAT_BINS - 1) b = HAT_BINS - 1;
        g_hat_hist[b]++;
        n++;
    }
    if (n == 0) return 0.0f;
    want = (unsigned) ceilf(SL_HAT_REST_QUANTILE * (float) n);
    for (i = 0; i < HAT_BINS; i++) {
        run += g_hat_hist[i];
        if (run >= want) {
            float d = (float) (i + 1) * 0.25f - 200.0f;   /* the bin's top */
            return d > 0.0f ? d : 0.0f;
        }
    }
    return 0.0f;
}


/* ---- THE DYNAMIC DOOR (src/sl_chr_asset.h) ------------------------------ */

int sl_door_takes(int door_flags, int door_type)
{
    return (door_flags & SL_DOOR_FLAG_CLIP_TO_BBOX) != 0
        && (door_type == SL_DOOR_TYPE_SLIDING || door_type == SL_DOOR_TYPE_VERTICAL);
}

int sl_door_rides(int door_flags, int door_type)
{
    return (door_flags & SL_DOOR_FLAG_CLIP_TO_BBOX) == 0
        && (door_type == SL_DOOR_TYPE_SLIDING || door_type == SL_DOOR_TYPE_VERTICAL
            || door_type == SL_DOOR_TYPE_SWINGING || door_type == SL_DOOR_TYPE_AZTECCHAIR);
}

int sl_door_parts(int door_flags, int door_type)
{
    return (door_flags & SL_DOOR_FLAG_CLIP_TO_BBOX) == 0
        && (door_type == SL_DOOR_TYPE_EYE || door_type == SL_DOOR_TYPE_IRIS);
}

int sl_door_path(int contract_on, int takes, int item_ready)
{
    if (!contract_on || !takes) return SL_DOOR_PATH_GAME;
    return item_ready ? SL_DOOR_PATH_ITEM : SL_DOOR_PATH_NATIVE;
}

int sl_door_cut_of(int door_type, const float box[6], struct sl_door_cut *out)
{
    memset(out, 0, sizeof *out);
    if (box == NULL) return 0;
    if (door_type == SL_DOOR_TYPE_VERTICAL) {         /* doorUpdateBbox: ymax falls */
        out->axis = 1; out->sign = -1; out->value = box[3];
    } else if (door_type == SL_DOOR_TYPE_SLIDING) {   /* doorUpdateBbox: xmin rises */
        out->axis = 0; out->sign = 1; out->value = box[0];
    } else {
        return 0;
    }
    if (!(out->value == out->value)) { memset(out, 0, sizeof *out); return 0; }
    out->on = 1;
    return 1;
}

int sl_door_item_fits(const void *asset)
{
    const struct sl_wa_asset *a = (const struct sl_wa_asset *) asset;
    return a != NULL && a->asset_class == SL_WA_CLASS_PROP && a->skin != NULL
        && a->joint_count == 1u && a->joints[0].parent < 0
        && a->vertex_count + a->index_count <= (unsigned) SL_DOOR_MAX_VERTS
        && 2u * a->index_count <= (unsigned) SL_DOOR_MAX_INDICES;
}

/* Joint 0's item-to-MODEL matrix, as sl_chr_joint_matrices forms it before
 * the game's matrix: T(-b) * scale(1/s) * Rz, row-vector. */
static void door_pre(const struct sl_chr_map *map, float pre[16])
{
    float cs, sn, inv = (map->scale != 0.0f) ? 1.0f / map->scale : 0.0f;
    const float *b = map->bind[0];
    cs = ((map->rot[0] & 3) == 0) ? 1.0f : ((map->rot[0] & 3) == 2) ? -1.0f : 0.0f;
    sn = ((map->rot[0] & 3) == 1) ? 1.0f : ((map->rot[0] & 3) == 3) ? -1.0f : 0.0f;
    memset(pre, 0, 16 * sizeof pre[0]);
    pre[0]  =  cs * inv; pre[1]  = sn * inv;
    pre[4]  = -sn * inv; pre[5]  = cs * inv;
    pre[10] = inv;
    pre[12] = (-b[0] * cs + b[1] * sn) * inv;
    pre[13] = (-b[0] * sn - b[1] * cs) * inv;
    pre[14] = -b[2] * inv;
    pre[15] = 1.0f;
}

static void door_lerp(struct sl_wa_vertex *o, const struct sl_wa_vertex *p,
                      const struct sl_wa_vertex *q, float t)
{
    o->x = p->x + t * (q->x - p->x);
    o->y = p->y + t * (q->y - p->y);
    o->z = p->z + t * (q->z - p->z);
    o->u = p->u + t * (q->u - p->u);
    o->v = p->v + t * (q->v - p->v);
    o->r = (unsigned char) ((float) p->r + t * ((float) q->r - (float) p->r) + 0.5f);
    o->g = (unsigned char) ((float) p->g + t * ((float) q->g - (float) p->g) + 0.5f);
    o->b = (unsigned char) ((float) p->b + t * ((float) q->b - (float) p->b) + 0.5f);
    o->a = (unsigned char) ((float) p->a + t * ((float) q->a - (float) p->a) + 0.5f);
}

static double door_area2(const struct sl_wa_vertex *p, const struct sl_wa_vertex *q,
                          const struct sl_wa_vertex *r)
{
    double e[3] = { q->x - p->x, q->y - p->y, q->z - p->z };
    double f[3] = { r->x - p->x, r->y - p->y, r->z - p->z };
    double c[3] = { e[1] * f[2] - e[2] * f[1], e[2] * f[0] - e[0] * f[2], e[0] * f[1] - e[1] * f[0] };
    return c[0] * c[0] + c[1] * c[1] + c[2] * c[2];
}

int sl_door_cut_mesh(const struct sl_wa_asset *a, const struct sl_chr_map *map,
                     const struct sl_door_cut *cut,
                     struct sl_wa_vertex *ov, struct sl_wa_skin *osk, unsigned vcap,
                     unsigned *oi, unsigned icap, struct sl_wa_mesh *om,
                     unsigned *nv, unsigned *ni)
{
    float pre[16], g[3], gg;
    unsigned n, m = 0, mi, t;
    int ax;

    if (nv) *nv = 0;
    if (ni) *ni = 0;
    if (!sl_door_item_fits(a) || map == NULL || !map->ok || map->count != 1
        || cut == NULL || !cut->on || (cut->axis != 0 && cut->axis != 1)
        || (cut->sign != 1 && cut->sign != -1)
        || vcap < a->vertex_count + a->index_count || icap < 2u * a->index_count)
        return 0;
    door_pre(map, pre);
    ax = cut->axis;
    /* the model coordinate on the cut axis is v . g + pre[12 + ax] */
    g[0] = pre[ax]; g[1] = pre[4 + ax]; g[2] = pre[8 + ax];
    gg = g[0] * g[0] + g[1] * g[1] + g[2] * g[2];
    if (!(gg > 0.0f)) return 0;
#define DOOR_D(vp) ((float) cut->sign * ((vp)->x * g[0] + (vp)->y * g[1] + (vp)->z * g[2] \
                                         + pre[12 + ax] - cut->value))
    memcpy(ov, a->vertices, a->vertex_count * sizeof ov[0]);
    memcpy(osk, a->skin, a->vertex_count * sizeof osk[0]);
    n = a->vertex_count;
    for (mi = 0; mi < a->mesh_count; mi++) {
        const struct sl_wa_mesh *mh = &a->meshes[mi];
        om[mi] = *mh;
        om[mi].first_index = m;
        if (mh->first_index + mh->index_count > a->index_count) return 0;
        for (t = 0; t + 2 < mh->index_count; t += 3) {
            const unsigned *k = a->indices + mh->first_index + t;
            float d[3];
            int j, in = 0;
            if (k[0] >= a->vertex_count || k[1] >= a->vertex_count || k[2] >= a->vertex_count)
                return 0;
            for (j = 0; j < 3; j++) {
                d[j] = DOOR_D(&a->vertices[k[j]]);
                if (d[j] >= 0.0f) in++;
            }
            if (in == 3) {                         /* wholly kept */
                oi[m++] = k[0]; oi[m++] = k[1]; oi[m++] = k[2];
            } else if (in == 0) {                  /* wholly past: laid onto the face */
                double before = door_area2(&a->vertices[k[0]], &a->vertices[k[1]], &a->vertices[k[2]]);
                for (j = 0; j < 3; j++) {
                    const struct sl_wa_vertex *p = &a->vertices[k[j]];
                    float s = ((float) cut->sign * d[j]) / gg;   /* model offset / |g|^2 */
                    ov[n + j] = *p;
                    ov[n + j].x = p->x - s * g[0];
                    ov[n + j].y = p->y - s * g[1];
                    ov[n + j].z = p->z - s * g[2];
                    osk[n + j] = a->skin[k[j]];
                }
                if (door_area2(&ov[n], &ov[n + 1], &ov[n + 2]) <= 1e-6 * before || before <= 0.0)
                    continue;                      /* edge-on to the face: nothing left */
                oi[m++] = n; oi[m++] = n + 1; oi[m++] = n + 2;
                n += 3;
            } else {                               /* across the face: clipped */
                unsigned poly[4];
                int c = 0;
                for (j = 0; j < 3; j++) {
                    int q = (j + 1) % 3;
                    if (d[j] >= 0.0f) poly[c++] = k[j];
                    if ((d[j] >= 0.0f) != (d[q] >= 0.0f)) {
                        float tt = d[j] / (d[j] - d[q]);
                        door_lerp(&ov[n], &a->vertices[k[j]], &a->vertices[k[q]], tt);
                        osk[n] = a->skin[d[j] >= 0.0f ? k[j] : k[q]];
                        poly[c++] = n++;
                    }
                }
                for (j = 2; j < c; j++) {
                    oi[m++] = poly[0]; oi[m++] = poly[j - 1]; oi[m++] = poly[j];
                }
            }
        }
        om[mi].index_count = m - om[mi].first_index;
    }
#undef DOOR_D
    if (nv) *nv = n;
    if (ni) *ni = m;
    return 1;
}
unsigned sl_prop_item_passes(const void *asset)
{
    const struct sl_wa_asset *a = (const struct sl_wa_asset *) asset;
    unsigned mi, out = 0;
    if (a == NULL) return 0;
    for (mi = 0; mi < a->mesh_count && mi < SL_WA_MAX_MESHES; mi++) {
        unsigned m = a->meshes[mi].material;
        if (m >= a->material_count) continue;
        out |= (a->materials[m].blend == SL_WA_BLEND_ALPHA) ? SL_CHR_PASS_TRANSLUCENT
                                                            : SL_CHR_PASS_SOLID;
    }
    return out;
}

int sl_chr_pass_draws(unsigned passes, int translucent)
{
    if (passes == 0u) return 1;
    return (passes & (translucent ? SL_CHR_PASS_TRANSLUCENT : SL_CHR_PASS_SOLID)) != 0u;
}

/* THE SCREEN'S PLACE (src/sl_chr_asset.h). */
static int scr_corner(float c)
{
    if (fabsf(c) <= 0.02f) return 0;
    if (fabsf(c - 1.0f) <= 0.02f) return 1;
    return -1;
}

int sl_screen_place(const void *asset, const struct sl_chr_map *map,
                    const float quad[4][3], float out[4][3])
{
    const struct sl_wa_asset *a = (const struct sl_wa_asset *) asset;
    float c[3], e0[3], e1[3], n[3], t[3], diag, inv, ln, le;
    float nb[4], best[4], bc[4][3], nxy[4][2];
    double best_iou = 0.0;
    unsigned mi, i, j;
    int k, found = 0;

    memcpy(out, quad, sizeof(float) * 12);
    if (a == NULL || map == NULL || map->count != 1 || map->rot[0] != 0
        || !(map->scale > 0.0f) || a->indices == NULL || a->vertices == NULL)
        return 0;
    inv = 1.0f / map->scale;
    for (k = 0; k < 3; k++) {
        c[k] = 0.25f * (quad[0][k] + quad[1][k] + quad[2][k] + quad[3][k]);
        e0[k] = quad[1][k] - quad[0][k];
        t[k] = quad[3][k] - quad[0][k];
    }
    n[0] = e0[1] * t[2] - e0[2] * t[1];
    n[1] = e0[2] * t[0] - e0[0] * t[2];
    n[2] = e0[0] * t[1] - e0[1] * t[0];
    ln = len3(n);
    le = len3(e0);
    if (!(ln > SL_CHR_TINY) || !(le > SL_CHR_TINY)) return 0;
    for (k = 0; k < 3; k++) { n[k] /= ln; e0[k] /= le; }
    e1[0] = n[1] * e0[2] - n[2] * e0[1];
    e1[1] = n[2] * e0[0] - n[0] * e0[2];
    e1[2] = n[0] * e0[1] - n[1] * e0[0];
    for (k = 0; k < 3; k++) t[k] = quad[2][k] - quad[0][k];
    diag = len3(t);
    nb[0] = nb[2] = 1e30f;
    nb[1] = nb[3] = -1e30f;
    for (i = 0; i < 4; i++) {
        float x = 0.0f, y = 0.0f;
        for (k = 0; k < 3; k++) { x += (quad[i][k] - c[k]) * e0[k]; y += (quad[i][k] - c[k]) * e1[k]; }
        if (x < nb[0]) nb[0] = x;
        if (x > nb[1]) nb[1] = x;
        if (y < nb[2]) nb[2] = y;
        if (y > nb[3]) nb[3] = y;
    }
    /* every pair of triangles of one draw laying the four corners of the
     * whole image */
    for (mi = 0; mi < a->mesh_count && mi < SL_WA_MAX_MESHES; mi++) {
        const struct sl_wa_mesh *mh = &a->meshes[mi];
        for (i = 0; i + 2 < mh->index_count; i += 3) {
            for (j = i + 3; j + 2 < mh->index_count; j += 3) {
                const unsigned *ti = a->indices + mh->first_index + i;
                const unsigned *tj = a->indices + mh->first_index + j;
                unsigned v[6], seen = 0, corner_of[4], m;
                int ok = 1, nc = 0;
                float bx0 = 1e30f, bx1 = -1e30f, by0 = 1e30f, by1 = -1e30f;
                double ix, iy, inter, uni;
                float cq[4][3];
                for (m = 0; m < 3; m++) { v[m] = ti[m]; v[m + 3] = tj[m]; }
                for (m = 0; m < 4; m++) corner_of[m] = 0xffffffffu;
                for (m = 0; m < 6 && ok; m++) {
                    const struct sl_wa_vertex *w;
                    int cu, cv, cid;
                    if (v[m] >= a->vertex_count) { ok = 0; break; }
                    w = &a->vertices[v[m]];
                    cu = scr_corner(w->u);
                    cv = scr_corner(w->v);
                    if (cu < 0 || cv < 0) { ok = 0; break; }
                    cid = cu + 2 * cv;
                    if (corner_of[cid] == 0xffffffffu) { corner_of[cid] = v[m]; seen |= 1u << cid; nc++; }
                }
                if (!ok || nc != 4 || seen != 15u) continue;
                for (m = 0; m < 4 && ok; m++) {
                    const struct sl_wa_vertex *w = &a->vertices[corner_of[m]];
                    float r[3], x = 0.0f, y = 0.0f, d = 0.0f;
                    r[0] = (w->x - map->bind[0][0]) * inv;
                    r[1] = (w->y - map->bind[0][1]) * inv;
                    r[2] = (w->z - map->bind[0][2]) * inv;
                    for (k = 0; k < 3; k++) {
                        x += (r[k] - c[k]) * e0[k];
                        y += (r[k] - c[k]) * e1[k];
                        d += (r[k] - c[k]) * n[k];
                    }
                    if (fabsf(d) > 0.05f * diag) ok = 0;
                    cq[m][0] = x; cq[m][1] = y; cq[m][2] = d;
                    if (x < bx0) bx0 = x;
                    if (x > bx1) bx1 = x;
                    if (y < by0) by0 = y;
                    if (y > by1) by1 = y;
                }
                if (!ok || !(bx1 - bx0 > 0.0f) || !(by1 - by0 > 0.0f)) continue;
                ix = (double) fminf(bx1, nb[1]) - (double) fmaxf(bx0, nb[0]);
                iy = (double) fminf(by1, nb[3]) - (double) fmaxf(by0, nb[2]);
                if (ix <= 0.0 || iy <= 0.0) continue;
                inter = ix * iy;
                uni = (double) (bx1 - bx0) * (by1 - by0)
                    + (double) (nb[1] - nb[0]) * (nb[3] - nb[2]) - inter;
                if (inter / uni > best_iou) {
                    best_iou = inter / uni;
                    best[0] = bx0; best[1] = bx1; best[2] = by0; best[3] = by1;
                    memcpy(bc, cq, sizeof bc);
                    found = 1;
                }
            }
        }
    }
    if (!found || best_iou < 0.5) return 0;
    if (fabsf(best[0] - nb[0]) < 0.001f * diag && fabsf(best[1] - nb[1]) < 0.001f * diag
        && fabsf(best[2] - nb[2]) < 0.001f * diag && fabsf(best[3] - nb[3]) < 0.001f * diag)
        return 0;
    for (i = 0; i < 4; i++) {
        float x = 0.0f, y = 0.0f, nx, ny;
        for (k = 0; k < 3; k++) { x += (quad[i][k] - c[k]) * e0[k]; y += (quad[i][k] - c[k]) * e1[k]; }
        nx = (x < 0.5f * (nb[0] + nb[1])) ? best[0] : best[1];
        ny = (y < 0.5f * (nb[2] + nb[3])) ? best[2] : best[3];
        nxy[i][0] = nx; nxy[i][1] = ny;
        for (k = 0; k < 3; k++)
            out[i][k] = quad[i][k] + (nx - x) * e0[k] + (ny - y) * e1[k];
    }
    /* IN FRONT OF THE ITEM'S FACE. The item's screen surface need not be
     * parallel to the game's quad, so the quad's own plane can cross it:
     * prop/console_sev2b's face stands 0.15 model units in front of that plane
     * at the top and 0.22 behind it at the bottom (prop/doorconsole 0.30 and
     * 0.34), and the vertices' whole units took the top 0.33 further back.
     * Where the face is in front it hides the screen beyond what the decal
     * priority covers, more of it the nearer the eye. So each corner is kept
     * in front of the item's corner under it by a margin - the front being
     * the side away from the item's box centre - and is set on whole units
     * that keep it there. A corner already that far in front keeps its
     * nearest whole units, which is what the seam's rounding gives. */
    {
        float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
        float side = 0.0f, fr, margin;
        unsigned q;
        for (q = 0; q < a->vertex_count; q++) {
            const struct sl_wa_vertex *w = &a->vertices[q];
            float r[3];
            r[0] = (w->x - map->bind[0][0]) * inv;
            r[1] = (w->y - map->bind[0][1]) * inv;
            r[2] = (w->z - map->bind[0][2]) * inv;
            for (k = 0; k < 3; k++) {
                if (r[k] < lo[k]) lo[k] = r[k];
                if (r[k] > hi[k]) hi[k] = r[k];
            }
        }
        for (k = 0; k < 3; k++) side += (c[k] - 0.5f * (lo[k] + hi[k])) * n[k];
        if (!(fabsf(side) > 0.01f * diag)) return 1;
        fr = (side > 0.0f) ? 1.0f : -1.0f;
        /* the item's face is two triangles; the margin covers its twist */
        margin = 0.0002f * diag + fabsf(bc[0][2] + bc[3][2] - bc[1][2] - bc[2][2]);
        for (i = 0; i < 4; i++) {
            float need, h = 0.0f, dmin = 1e30f, cand[3], pick[3];
            unsigned m, under = 0, bits;
            for (m = 0; m < 4; m++) {
                float dx = bc[m][0] - nxy[i][0], dy = bc[m][1] - nxy[i][1];
                if (dx * dx + dy * dy < dmin) { dmin = dx * dx + dy * dy; under = m; }
            }
            need = fr * bc[under][2] + margin;
            for (k = 0; k < 3; k++) h += (out[i][k] - c[k]) * n[k];
            h *= fr;
            if (h < need)
                for (k = 0; k < 3; k++) out[i][k] += n[k] * fr * (need - h);
            dmin = 1e30f;
            for (k = 0; k < 3; k++) pick[k] = floorf(out[i][k] + 0.5f);
            for (bits = 0; bits < 8u; bits++) {
                float hc = 0.0f, dd = 0.0f;
                for (k = 0; k < 3; k++) {
                    cand[k] = ((bits >> k) & 1u) ? ceilf(out[i][k]) : floorf(out[i][k]);
                    hc += (cand[k] - c[k]) * n[k];
                    dd += (cand[k] - out[i][k]) * (cand[k] - out[i][k]);
                }
                if (fr * hc >= need && dd < dmin) { dmin = dd; memcpy(pick, cand, sizeof pick); }
            }
            memcpy(out[i], pick, sizeof pick);
        }
    }
    return 1;
}

int sl_prop_item_moves(const void *asset)
{
    const struct sl_wa_asset *a = (const struct sl_wa_asset *) asset;
    unsigned j;
    if (a == NULL || a->asset_class != SL_WA_CLASS_PROP) return 0;
    for (j = 0; j < a->joint_count; j++)
        if (a->joints[j].part > SL_WA_PART_MATRIX) return 1;
    return 0;
}

/* THE FIXED PARTS (src/sl_chr_asset.h). */
int sl_prop_parts_fixed(int num_matrices, int moving_nodes, int simple_matrices)
{
    return num_matrices > 1 && moving_nodes == 0 && simple_matrices == num_matrices;
}

/* THE DESTROYED AUTOGUN (src/sl_chr_asset.h). */
int sl_prop_destroyed_keeps(int kind)
{
    return kind == SL_PROP_KIND_AUTOGUN;
}

/* The deformed part's surviving brightness after `level` blackenings:
 * objDeform blackens a vertex at or above the part's mid-height nine times
 * in ten and one below one time in five, so the upper half keeps 0.1^L of
 * its colour and the lower 0.8^L. */
int sl_prop_destroyed_shade(int level, int above)
{
    double s = 1.0;
    int l;
    if (level <= 0) return 255;
    for (l = 0; l < level && l < 16; l++) s *= above ? 0.1 : 0.8;
    return (int) (s * 255.0 + 0.5);
}

/* An item point's height along the MODEL's y, up to the map's positive
 * scale: the joint's relation turns the item by quarter turns about z
 * (door_pre), so the model's y is sn * (x - bx) + cs * (y - by). */
float sl_prop_destroyed_height(const struct sl_chr_map *map, float x, float y)
{
    float cs, sn;
    if (map == NULL || map->count < 1) return y;
    cs = ((map->rot[0] & 3) == 0) ? 1.0f : ((map->rot[0] & 3) == 2) ? -1.0f : 0.0f;
    sn = ((map->rot[0] & 3) == 1) ? 1.0f : ((map->rot[0] & 3) == 3) ? -1.0f : 0.0f;
    return sn * (x - map->bind[0][0]) + cs * (y - map->bind[0][1]);
}

/* Does item vertex `vertex` ride game matrix `mtx`? By the heaviest skin
 * lane's joint through the map; an item with no skin rides its one joint. */
int sl_prop_destroyed_vertex(const void *asset, const struct sl_chr_map *map,
                             unsigned vertex, int mtx)
{
    const struct sl_wa_asset *a = (const struct sl_wa_asset *) asset;
    unsigned l, best = 0, j;
    if (a == NULL || map == NULL || mtx < 0 || vertex >= a->vertex_count) return 0;
    if (a->skin == NULL) return map->count > 0 && map->mtx[0] == mtx;
    for (l = 1; l < SL_WA_SKIN_LANES; l++)
        if (a->skin[vertex].weight[l] > a->skin[vertex].weight[best]) best = l;
    j = a->skin[vertex].joint[best];
    if ((int) j >= map->count) return 0;
    return map->mtx[j] == mtx;
}

/* THE PROP (src/sl_chr_asset.h). The item's reference box is the game's own
 * model in the item's space, so the relation is the scale that takes one box
 * to the other on every axis and the offset between their centres; the item
 * joint then sits where that offset puts the model's root.
 *
 * THE MOVING PROP: the same relation places the item; each joint that names
 * a matrix is set where it puts the model's node for that matrix, so the
 * part turns about the game's own pivot; a joint that names none rides the
 * root. The part's own joint (the first joint of its chain to name the
 * matrix) must sit within SL_PROP_PIVOT_TOL of that node - a joint named for
 * a part the model places elsewhere is refused.
 *
 * A JOINT WITH NO PIVOT OF ITS OWN - one that names a matrix of its own but
 * sits on its parent's pivot (within SL_PROP_SAME_PIVOT) - says nothing about
 * where its part turns, so where the pivot test would refuse it, it is not
 * refused but rides its parent's matrix, its parent's bind with it; a joint
 * the test accepts is mapped as before. The item's vertices keep their
 * rest positions, so the part stays where the remaster put it; only a turn the
 * game gave that matrix apart from its parent's would be lost. MEASURED on the
 * remaster's ceiling gun (prop/roofgun, Bunker 2's ProofgunZ, an autogun):
 * SKEL_CARTRIDGE sits exactly on SKEL_XROT (0, -23.96, 0) while the model's
 * node for matrix 4 lies 242 item units away (tolerance 92), so the item was
 * refused whole. The game never turns that matrix apart from its parent: the
 * autogun branch of the object render sets it to identity and its node's
 * position under matrix 2 (propobj.c, Switches[4]), so riding matrix 2 places
 * it exactly where matrix 4 would. */
/* #94 THE PARTS BY PLACE. The bundle names each joint's matrix by the game's
 * NAME for it (the model skeleton's joint list), and two parts the game
 * builds alike can trade names: Jungle's drone gun (PgroundgunZ, the
 * autogun skeleton: TOP, YROT, XROT, BARREL, CARTRIDGE) turns two barrels,
 * matrix 3 (Switches[3]) and matrix 4 (Switches[6]), each about its own x
 * axis by the same angle (propobj.c, the autogun branch), at model nodes
 * (-1000, 177.2, -247.2) and (-1000, 177.2, 247.2). The remaster's item
 * joints its barrels SKEL_BARREL at (-500, 88.6, 123.6) and SKEL_BAR2 at
 * (-500, 88.6, -123.6) - the two nodes exactly, at the item's scale 0.5 -
 * so the name put SKEL_BARREL on matrix 3, a whole barrel spacing (247 item
 * units, tolerance 116) from its node, the item was refused and the game's
 * model drew; SKEL_BAR2, named for no matrix, would have ridden the pitch
 * without turning.
 *
 * So where the names are refused, the item is related by PLACE instead, and
 * only when every part of it agrees: every joint that rides a matrix must
 * sit within SL_PROP_SAME_PIVOT of the model's node for some present matrix
 * (the nearest), and rides that matrix with its joint on that node; every
 * matrix a joint named must still be ridden by some joint; a joint that
 * rides no matrix rides the root as before. One joint that sits nowhere
 * exact keeps the refusal. An item the names already relate never reaches
 * here, so every accepted item maps exactly as before. 1 mapped, 0 not. */
static int prop_parts_by_place(const struct sl_wa_asset *a, const struct sl_prop_box *game,
                               const float rb[3], float scale, double lr,
                               struct sl_chr_map *out)
{
    unsigned char named[SL_PROP_MAX_MTX], rode[SL_PROP_MAX_MTX];
    int j, k, m;
    memset(named, 0, sizeof named);
    memset(rode, 0, sizeof rode);
    for (j = 0; j < (int) a->joint_count; j++)
        if (a->joints[j].part > SL_WA_PART_MATRIX) {
            m = (int) (a->joints[j].part - SL_WA_PART_MATRIX - 1u);
            if (m < 0 || m >= SL_PROP_MAX_MTX) return 0;
            named[m] = 1;
        }
    for (j = 0; j < (int) a->joint_count; j++) {
        const struct sl_wa_joint *jt = &a->joints[j];
        float nb[3] = { 0.0f, 0.0f, 0.0f };
        double bd = -1.0;
        int best = -1;
        out->rot[j] = 0;
        if (jt->part <= SL_WA_PART_MATRIX) {
            out->mtx[j] = game->root_mtx;
            memcpy(out->bind[j], rb, sizeof out->bind[j]);
            continue;
        }
        for (m = 0; m < game->mtx_count && m < SL_PROP_MAX_MTX; m++) {
            float node[3], d[3];
            double dd;
            if (!game->node_present[m]) continue;
            for (k = 0; k < 3; k++) {
                node[k] = rb[k] + scale * (game->node_origin[m][k] - game->root_origin[k]);
                d[k] = jt->bind[k] - node[k];
            }
            dd = len3(d);
            if (best < 0 || dd < bd) { best = m; bd = dd; memcpy(nb, node, sizeof nb); }
        }
        if (best < 0 || bd > SL_PROP_SAME_PIVOT * lr) return 0;
        out->mtx[j] = best;
        memcpy(out->bind[j], nb, sizeof out->bind[j]);
        rode[best] = 1;
    }
    for (m = 0; m < SL_PROP_MAX_MTX; m++)
        if (named[m] && !rode[m]) return 0;
    return 1;
}

/* #96 THE PARTS ABOUT THEIR OWN PIVOTS. Where neither the names nor the
 * places relate an item, its moving parts may still be the model's parts
 * BUILT ELSEWHERE: the remaster's Cradle helicopter (prop/helicopter, the
 * game's PhelicopterZ) is a different build of the aircraft, related to the
 * game's model by its reference box (scale 1.538). MEASURED: its tail rotor
 * (SKEL_TAILROTOR, matrix 3, 635 vertices centred on its joint at 123.8 380
 * -14386) sits 2633 item units from the model's tail-rotor node (124.0 1495.8
 * -16770.8; tolerance 1387) - the boom 5168 behind the main rotor where the
 * model's is 6299, the rotor 620 below the hub where the model's is level -
 * and its main rotor 1297 from the model's (the item's x on both is the
 * model's, to 0.2). Put on the model's nodes the rotors would turn off the
 * ends of the item's own boom and mast; riding the root they would stand
 * still.
 *
 * (The main rotor is inside the pivot tolerance, which is why the tail
 * rotor alone refused the item - but turned about the model's hub, the
 * item's rotor disc would swing 1255 item units off its own mast.)
 *
 * So each such part turns about ITS OWN joint by the turn the game gives
 * its matrix relative to the root's (sl_chr_joint_matrices): the part stays
 * where the remaster built it, under the root's placement, and spins as the
 * game spins it; a joint exactly on its node (within SL_PROP_SAME_PIVOT) is
 * mapped to the node as the names map it. Taken only for an ANIMATED model
 * (rooted on a HEADER node: the aircraft, sl_prop_box.animated), and only
 * where every joint that rides a matrix is nearest (to within
 * SL_PROP_SAME_PIVOT) to the model's node for the matrix its name gives it
 * and sits inside the item's box: a part named for a node that lies nearer
 * another part's keeps the refusal. A joint whose parent rides the same
 * matrix moves with that parent. An item the names or places relate never
 * reaches here. 1 mapped, 0 not. */
static int prop_parts_own_pivot(const struct sl_wa_asset *a, const struct sl_prop_box *game,
                                const float rb[3], float scale, double lr,
                                struct sl_chr_map *out)
{
    int j, k, m;
    if (!game->animated || !(scale > SL_CHR_TINY)) return 0;
    for (j = 0; j < (int) a->joint_count; j++) {
        const struct sl_wa_joint *jt = &a->joints[j];
        double dn = -1.0, dmin = -1.0;
        out->rot[j] = 0;
        out->own_pivot[j] = 0;
        if (jt->part <= SL_WA_PART_MATRIX) {
            out->mtx[j] = game->root_mtx;
            memcpy(out->bind[j], rb, sizeof out->bind[j]);
            continue;
        }
        m = (int) (jt->part - SL_WA_PART_MATRIX - 1u);
        if (m < 0 || m >= game->mtx_count || m >= SL_PROP_MAX_MTX || !game->node_present[m])
            return 0;
        if (jt->parent >= 0 && jt->parent < j && a->joints[jt->parent].part == jt->part) {
            int p = jt->parent;
            out->mtx[j] = out->mtx[p];
            out->own_pivot[j] = out->own_pivot[p];
            memcpy(out->bind[j], out->bind[p], sizeof out->bind[j]);
            memcpy(out->carry[j], out->carry[p], sizeof out->carry[j]);
            continue;
        }
        for (k = 0; k < 3; k++)
            if (jt->bind[k] < a->bounds_min[k] - SL_PROP_SAME_PIVOT * lr
                || jt->bind[k] > a->bounds_max[k] + SL_PROP_SAME_PIVOT * lr)
                return 0;
        {
            int q;
            for (q = 0; q < game->mtx_count && q < SL_PROP_MAX_MTX; q++) {
                float d[3];
                double dd;
                if (!game->node_present[q]) continue;
                for (k = 0; k < 3; k++)
                    d[k] = jt->bind[k] - (rb[k] + scale * (game->node_origin[q][k]
                                                           - game->root_origin[k]));
                dd = len3(d);
                if (q == m) dn = dd;
                else if (dmin < 0.0 || dd < dmin) dmin = dd;
            }
        }
        if (dn < 0.0 || (dmin >= 0.0 && dn > dmin + SL_PROP_SAME_PIVOT * lr)) return 0;
        out->mtx[j] = m;
        if (dn <= SL_PROP_SAME_PIVOT * lr) {
            /* exactly on the model's node: mapped to it, as the names map it */
            for (k = 0; k < 3; k++)
                out->bind[j][k] = rb[k] + scale * (game->node_origin[m][k] - game->root_origin[k]);
            continue;
        }
        out->own_pivot[j] = 1;
        memcpy(out->bind[j], jt->bind, sizeof out->bind[j]);
        for (k = 0; k < 3; k++)
            out->carry[j][k] = (jt->bind[k] - rb[k]) / scale
                               - (game->node_origin[m][k] - game->root_origin[k]);
    }
    out->pivot_root = game->root_mtx;
    return 1;
}

int sl_prop_derive(const void *asset, const struct sl_prop_box *game,
                   struct sl_chr_map *out, char *why)
{
    const struct sl_wa_asset *a = (const struct sl_wa_asset *) asset;
    float er[3], eg[3], cr[3], cg[3], scale;
    double lr, lg;
    int k, moves;

    memset(out, 0, sizeof *out);
    if (why) why[0] = '\0';
#define REFUSE(...) do { if (why) snprintf(why, 96, __VA_ARGS__); return 0; } while (0)
    if (a == NULL || game == NULL) REFUSE("no item or no model");
    if (a->asset_class != SL_WA_CLASS_PROP && !sl_vm_item_static(asset))
        REFUSE("the item is neither a prop nor a static view model");
    moves = sl_prop_item_moves(asset);
    if (a->skin == NULL || a->joint_count == 0u || a->joint_count > SL_CHR_MAX_JOINTS
        || (!moves && (a->joint_count != 1u || a->joints[0].parent >= 0)))
        REFUSE("a prop item is one rigid piece on one root joint");
    if (game->root_mtx < 0 || game->root_mtx >= game->mtx_count)
        REFUSE("the model has no root matrix");
    for (k = 0; k < 3; k++) {
        er[k] = a->bounds_max[k] - a->bounds_min[k];
        eg[k] = game->hi[k] - game->lo[k];
        cr[k] = 0.5f * (a->bounds_max[k] + a->bounds_min[k]);
        cg[k] = 0.5f * (game->hi[k] + game->lo[k]);
        if (!(er[k] >= 0.0f) || !(eg[k] >= 0.0f)) REFUSE("an empty box");
    }
    lr = len3(er);
    lg = len3(eg);
    if (!(lr > SL_CHR_TINY) || !(lg > SL_CHR_TINY)) REFUSE("no extent to take a scale from");
    scale = (float) (lr / lg);
    for (k = 0; k < 3; k++)
        if (fabs((double) (er[k] - scale * eg[k])) > SL_CHR_SCALE_TOL * lr) break;
    if (k < 3) {
        /* THE TURNED ITEM (src/sl_chr_asset.h): the box under a quarter
         * turn, the turn chosen by where the vertices' mean lands */
        int bad = k, t, turn = -1, lng = (eg[0] >= eg[1]) ? 0 : 1;
        float side[2];
        /* THE FITTED ITEM (D-038, src/sl_chr_asset.h) */
        if (!moves && game->fit_ok) {
            float fs = 0.0f;
            for (k = 0; k < 3; k++) {
                if (!(eg[k] > SL_CHR_TINY)) REFUSE("the model's box is flat on axis %d", k);
                if (er[k] / eg[k] > fs) fs = er[k] / eg[k];
            }
            out->count = 1;
            out->scale = fs;
            out->mtx[0] = game->root_mtx;
            out->rot[0] = 0;
            out->bind[0][0] = cr[0] - fs * (cg[0] - game->root_origin[0]);
            out->bind[0][1] = a->bounds_min[1] - fs * (game->lo[1] - game->root_origin[1]);
            out->bind[0][2] = cr[2] - fs * (cg[2] - game->root_origin[2]);
            goto mapped;
        }
        if (moves || !game->turn_ok)
            REFUSE("the item's box is not the model's on axis %d", bad);
        if (fabs((double) (er[0] - scale * eg[1])) > SL_CHR_SCALE_TOL * lr
            || fabs((double) (er[1] - scale * eg[0])) > SL_CHR_SCALE_TOL * lr
            || fabs((double) (er[2] - scale * eg[2])) > SL_CHR_SCALE_TOL * lr)
            REFUSE("the item's box is not the model's on axis %d, turned or not", bad);
        for (t = 0; t < 2; t++) {
            /* the item's box centre from its origin, turned and scaled: on
             * which side of the grip it lies along the model's long axis */
            float d[3], p[3];
            for (k = 0; k < 3; k++) d[k] = cr[k] / scale;
            rot_apply(t == 0 ? 1 : 3, d, p);
            side[t] = p[lng];
        }
        if (!(fabsf(cg[lng]) >= SL_PROP_GRIP_SIDE * eg[lng]) || side[0] * side[1] >= 0.0f)
            REFUSE("the item's box is the model's turned either way; neither lays it out from the grip");
        turn = (side[0] * cg[lng] > 0.0f) ? 1 : 3;
        {
            float g[3], d[3];
            for (k = 0; k < 3; k++) g[k] = cg[k] - game->root_origin[k];
            rot_apply(4 - turn, g, d);
            out->count = 1;
            out->scale = scale;
            out->mtx[0] = game->root_mtx;
            out->rot[0] = turn;
            for (k = 0; k < 3; k++) out->bind[0][k] = cr[k] - scale * d[k];
        }
        goto mapped;
    }
    out->count = 1;
    out->scale = scale;
    out->mtx[0] = game->root_mtx;
    out->rot[0] = 0;
    for (k = 0; k < 3; k++)
        out->bind[0][k] = cr[k] - scale * (cg[k] - game->root_origin[k]);
    if (moves) {
        float rb[3];
        int j;
        memcpy(rb, out->bind[0], sizeof rb);
        out->count = (int) a->joint_count;
        for (j = 0; j < (int) a->joint_count; j++) {
            const struct sl_wa_joint *jt = &a->joints[j];
            out->rot[j] = 0;
            if (jt->part <= SL_WA_PART_MATRIX) {
                out->mtx[j] = game->root_mtx;
                memcpy(out->bind[j], rb, sizeof rb);
                continue;
            }
            {
                int m = (int) (jt->part - SL_WA_PART_MATRIX - 1u);
                float d[3];
                if (m >= game->mtx_count || m >= SL_PROP_MAX_MTX || !game->node_present[m])
                    REFUSE("%s names matrix %d, under which the model positions no part",
                           jt->name, m);
                out->mtx[j] = m;
                for (k = 0; k < 3; k++) {
                    out->bind[j][k] = rb[k] + scale * (game->node_origin[m][k]
                                                       - game->root_origin[k]);
                    d[k] = jt->bind[k] - out->bind[j][k];
                }
                if ((jt->parent < 0 || a->joints[jt->parent].part != jt->part)
                    && (double) len3(d) > SL_PROP_PIVOT_TOL * lr) {
                    /* A JOINT WITH NO PIVOT OF ITS OWN (see the comment
                     * above the function) rides its parent's matrix. */
                    float pd[3];
                    int own = (jt->parent < 0 || jt->parent >= j);
                    if (!own) {
                        for (k = 0; k < 3; k++)
                            pd[k] = jt->bind[k] - a->joints[jt->parent].bind[k];
                        own = (double) len3(pd) > SL_PROP_SAME_PIVOT * lr;
                    }
                    if (own) {
                        /* #94 THE PARTS BY PLACE (above) */
                        if (prop_parts_by_place(a, game, rb, scale, lr, out)) goto mapped;
                        if (prop_parts_own_pivot(a, game, rb, scale, lr, out)) goto mapped;
                        REFUSE("%s does not sit where the model's part for matrix %d sits",
                               jt->name, m);
                    }
                    out->mtx[j] = out->mtx[jt->parent];
                    memcpy(out->bind[j], out->bind[jt->parent], sizeof out->bind[j]);
                }
            }
        }
    }
mapped:
#undef REFUSE
    out->ok = 1;
    return 1;
}

int sl_chr_item_owns_head(const void *asset)
{
    const struct sl_wa_asset *a = (const struct sl_wa_asset *) asset;
    return a != NULL && a->asset_class == SL_WA_CLASS_CHAR && a->mesh_count > 0u
        && (a->meshes[0].flags & SL_WA_MESH_OWN_HEAD) != 0u;
}

int sl_vm_mesh_visible(unsigned flags, const unsigned switch_visible[2])
{
    unsigned k, c[2];
    c[0] = SL_WA_MESH_SWITCH_A(flags);
    c[1] = SL_WA_MESH_SWITCH_B(flags);
    for (k = 0; k < 2; k++) {
        unsigned s;
        if (c[k] == 0u) continue;
        s = c[k] - 1u;
        if (s >= SL_WA_MAX_SWITCH) return 0;
        if (!((switch_visible[s >> 5] >> (s & 31u)) & 1u)) return 0;
    }
    return 1;
}

int sl_vm_draw_visible(unsigned flags, const unsigned switch_visible[2], int alternatives)
{
    if (flags & SL_WA_MESH_ALT) {
        if (alternatives == SL_VM_ALT_ALL) return 1;
        if (SL_WA_MESH_SWITCH_A(flags) == 0u && SL_WA_MESH_SWITCH_B(flags) == 0u) return 0;
    }
    return sl_vm_mesh_visible(flags, switch_visible);
}

int sl_vm_alt_serves(const void *asset, int sw)
{
    const struct sl_wa_asset *a = (const struct sl_wa_asset *) asset;
    unsigned mi;
    if (a == NULL || a->asset_class != SL_WA_CLASS_GUN || sw < 0 || sw >= (int) SL_WA_MAX_SWITCH)
        return 0;
    for (mi = 0; mi < a->mesh_count; mi++) {
        unsigned f = a->meshes[mi].flags;
        if ((f & SL_WA_MESH_ALT) && (SL_WA_MESH_SWITCH_A(f) == (unsigned) sw + 1u
                                     || SL_WA_MESH_SWITCH_B(f) == (unsigned) sw + 1u))
            return 1;
    }
    return 0;
}

int sl_vm_item_static(const void *asset)
{
    const struct sl_wa_asset *a = (const struct sl_wa_asset *) asset;
    return a != NULL && a->asset_class == SL_WA_CLASS_GUN && a->joint_count == 1u
        && a->joints[0].parent < 0 && a->joints[0].part == 0u;
}

int sl_vm_item_gates(const void *asset, int first, int count)
{
    const struct sl_wa_asset *a = (const struct sl_wa_asset *) asset;
    unsigned mi;
    if (a == NULL || a->asset_class != SL_WA_CLASS_GUN) return 0;
    for (mi = 0; mi < a->mesh_count; mi++) {
        unsigned c[2], k;
        c[0] = SL_WA_MESH_SWITCH_A(a->meshes[mi].flags);
        c[1] = SL_WA_MESH_SWITCH_B(a->meshes[mi].flags);
        for (k = 0; k < 2; k++)
            if (c[k] != 0u && (int) c[k] - 1 >= first && (int) c[k] - 1 < first + count)
                return 1;
    }
    return 0;
}

static void mat_mul(const float a[16], const float b[16], float o[16])
{
    int r, c, k;
    float t[16];
    for (r = 0; r < 4; r++)
        for (c = 0; c < 4; c++) {
            float s = 0.0f;
            for (k = 0; k < 4; k++) s += a[r * 4 + k] * b[k * 4 + c];
            t[r * 4 + c] = s;
        }
    memcpy(o, t, sizeof t);
}

/* The inverse of an affine row-vector matrix (rows 0..2 the images of x, y,
 * z, row 3 the translation, column 3 0 0 0 1); 0 where it is singular. */
static int aff_inverse(const float m[16], float o[16])
{
    double a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6];
    double g = m[8], h = m[9], i = m[10];
    double A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
    double det = a * A + b * B + c * C, r;
    int k;
    if (!(fabs(det) > 1e-20)) return 0;
    r = 1.0 / det;
    o[0] = (float) (A * r);  o[1] = (float) (-(b * i - c * h) * r); o[2]  = (float) ((b * f - c * e) * r);
    o[4] = (float) (B * r);  o[5] = (float) ((a * i - c * g) * r);  o[6]  = (float) (-(a * f - c * d) * r);
    o[8] = (float) (C * r);  o[9] = (float) (-(a * h - b * g) * r); o[10] = (float) ((a * e - b * d) * r);
    o[3] = o[7] = o[11] = 0.0f;
    for (k = 0; k < 3; k++)
        o[12 + k] = -(m[12] * o[k] + m[13] * o[4 + k] + m[14] * o[8 + k]);
    o[15] = 1.0f;
    return 1;
}

void sl_chr_joint_matrices(const struct sl_chr_map *map,
                           const float (*game)[16], int game_count,
                           float (*out)[16])
{
    int j;
    for (j = 0; j < map->count; j++) {
        float pre[16], cs, sn, inv = (map->scale != 0.0f) ? 1.0f / map->scale : 0.0f;
        const float *b = map->bind[j];
        int m = map->mtx[j];
        if (m < 0 || m >= game_count) { memset(out[j], 0, sizeof out[j]); continue; }
        cs = ((map->rot[j] & 3) == 0) ? 1.0f : ((map->rot[j] & 3) == 2) ? -1.0f : 0.0f;
        sn = ((map->rot[j] & 3) == 1) ? 1.0f : ((map->rot[j] & 3) == 3) ? -1.0f : 0.0f;
        /* T(-b) * scale(1/s) * Rz, row-vector: rows are the images of x, y,
         * z and the translation. */
        memset(pre, 0, sizeof pre);
        pre[0]  =  cs * inv; pre[1]  = sn * inv;
        pre[4]  = -sn * inv; pre[5]  = cs * inv;
        pre[10] = inv;
        pre[12] = (-b[0] * cs + b[1] * sn) * inv;
        pre[13] = (-b[0] * sn - b[1] * cs) * inv;
        pre[14] = -b[2] * inv;
        pre[15] = 1.0f;
        mat_mul(pre, game[m], out[j]);
        /* #96 THE PART ABOUT ITS OWN PIVOT: the matrix's turn relative to
         * the root's, about the item's joint - T(-b) S(1/s) G[m] G[r]^-1
         * T(carry) G[r]; carry 0 is the line above exactly. */
        if (map->own_pivot[j] && map->pivot_root >= 0 && map->pivot_root < game_count) {
            float inv_r[16], rel[16];
            const float *gr = game[map->pivot_root];
            if (aff_inverse(gr, inv_r)) {
                mat_mul(out[j], inv_r, rel);
                rel[12] += map->carry[j][0];
                rel[13] += map->carry[j][1];
                rel[14] += map->carry[j][2];
                mat_mul(rel, gr, out[j]);
            }
        }
    }
}

void sl_chr_skin_vertices(const struct sl_wa_asset *a, const float (*joint)[16],
                          float *eye)
{
    unsigned i, l;
    for (i = 0; i < a->vertex_count; i++) {
        const struct sl_wa_vertex *v = &a->vertices[i];
        const struct sl_wa_skin *s = &a->skin[i];
        float x = 0.0f, y = 0.0f, z = 0.0f;
        for (l = 0; l < SL_WA_SKIN_LANES; l++) {
            const float *m;
            float w;
            if (s->weight[l] == 0) continue;
            m = joint[s->joint[l]];
            w = (float) s->weight[l] * (1.0f / 255.0f);
            x += w * (v->x * m[0] + v->y * m[4] + v->z * m[8]  + m[12]);
            y += w * (v->x * m[1] + v->y * m[5] + v->z * m[9]  + m[13]);
            z += w * (v->x * m[2] + v->y * m[6] + v->z * m[10] + m[14]);
        }
        eye[3 * i + 0] = x;
        eye[3 * i + 1] = y;
        eye[3 * i + 2] = z;
    }
}

void sl_vm_item_normals(const struct sl_wa_asset *a, float *item_n)
{
    unsigned i, k;
    for (i = 0; i < 3u * a->vertex_count; i++) item_n[i] = 0.0f;
    for (i = 0; i + 2u < a->index_count; i += 3u) {
        const struct sl_wa_vertex *p[3];
        float e1[3], e2[3], n[3];
        for (k = 0; k < 3u; k++) p[k] = &a->vertices[a->indices[i + k]];
        e1[0] = p[1]->x - p[0]->x; e1[1] = p[1]->y - p[0]->y; e1[2] = p[1]->z - p[0]->z;
        e2[0] = p[2]->x - p[0]->x; e2[1] = p[2]->y - p[0]->y; e2[2] = p[2]->z - p[0]->z;
        n[0] = e1[1] * e2[2] - e1[2] * e2[1];
        n[1] = e1[2] * e2[0] - e1[0] * e2[2];
        n[2] = e1[0] * e2[1] - e1[1] * e2[0];
        for (k = 0; k < 3u; k++) {
            float *o = &item_n[3u * a->indices[i + k]];
            o[0] += n[0]; o[1] += n[1]; o[2] += n[2];
        }
    }
    for (i = 0; i < a->vertex_count; i++) {
        float *o = &item_n[3u * i];
        float l = len3(o);
        if (l > SL_CHR_TINY) { o[0] /= l; o[1] /= l; o[2] /= l; }
        else { o[0] = 0.0f; o[1] = 0.0f; o[2] = 1.0f; }
    }
}

void sl_vm_sphere_coords(const struct sl_wa_asset *a, const float (*joint)[16],
                         const float *item_n, float *uv)
{
    unsigned i, l;
    for (i = 0; i < a->vertex_count; i++) {
        const struct sl_wa_skin *s = &a->skin[i];
        const float *n = &item_n[3u * i];
        float e[3] = { 0.0f, 0.0f, 0.0f }, len;
        for (l = 0; l < SL_WA_SKIN_LANES; l++) {
            const float *m;
            float w;
            if (s->weight[l] == 0) continue;
            m = joint[s->joint[l]];
            w = (float) s->weight[l] * (1.0f / 255.0f);
            e[0] += w * (n[0] * m[0] + n[1] * m[4] + n[2] * m[8]);
            e[1] += w * (n[0] * m[1] + n[1] * m[5] + n[2] * m[9]);
            e[2] += w * (n[0] * m[2] + n[1] * m[6] + n[2] * m[10]);
        }
        len = len3(e);
        if (len > SL_CHR_TINY) { e[0] /= len; e[1] /= len; }
        else { e[0] = 0.0f; e[1] = 0.0f; }
        uv[2u * i + 0] = 0.5f + 0.5f * e[0];
        uv[2u * i + 1] = 0.5f - 0.5f * e[1];
    }
}

/* THE COMPOSITION (src/sl_chr_asset.h). A character with no separate head
 * draws its replacement body whole, built-in head block included. One that
 * wears a separate head takes the neck from whichever head is drawn - the
 * replacement head's own, or, under the game's head, the game's body's - so
 * the replacement body is drawn only beside a replacement head; a
 * replacement head on the game's body closes its own neck over the game's. */
unsigned sl_chr_compose(int wears_head, int body, int head)
{
    if (!wears_head)
        return body ? (SL_CHR_DRAW_BODY | SL_CHR_DRAW_BUILTIN_HEAD) : 0u;
    if (!head)
        return 0u;
    return body ? (SL_CHR_DRAW_BODY | SL_CHR_DRAW_HEAD) : SL_CHR_DRAW_HEAD;
}

/* =========================================================================
 * SELF-TEST (-DSL_CHR_SELFTEST), synthetic skeletons only.
 * ========================================================================= */
#ifdef SL_CHR_SELFTEST
#include <stdarg.h>
#include <stdlib.h>

static int t_pass, t_fail;
static void ck(int cond, const char *fmt, ...)
{
    va_list ap;
    if (cond) { t_pass++; return; }
    t_fail++;
    va_start(ap, fmt);
    printf("FAIL: ");
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
}

const char *sl_asset_override_dir(void) { return "."; }

static struct sl_wa_asset g_a;
static struct sl_wa_vertex g_v[4];
static struct sl_wa_skin g_s[4];

static void joint(int j, const char *name, int parent, float x, float y, float z)
{
    strcpy(g_a.joints[j].name, name);
    g_a.joints[j].parent = parent;
    g_a.joints[j].bind[0] = x; g_a.joints[j].bind[1] = y; g_a.joints[j].bind[2] = z;
}

/* item: ROOT -> HUB -> {UP (0,10), LEFT (10,-10) -> LEFTLOW (10,-30), RIGHT (-10,-10)}
 * game (scale 5, the left limb laid along +x): hub -> {up (0,2), left (2,-2)
 * -> leftlow (4,0), right (-2,-2)} */
static void fixture(void)
{
    memset(&g_a, 0, sizeof g_a);
    g_a.joint_count = 6;
    joint(0, "SKEL_ROOT", -1, 0, 0, 0);
    joint(1, "SKEL_HUB", 0, 0, 0, 0);
    joint(2, "SKEL_UP", 1, 0, 10, 0);
    joint(3, "SKEL_LEFT", 1, 10, -10, 0);
    joint(4, "SKEL_RIGHT", 1, -10, -10, 0);
    joint(5, "SKEL_LEFTLOW", 3, 10, -30, 0);
    g_a.vertex_count = 2;
    g_a.vertices = g_v;
    g_a.skin = g_s;
    memset(g_v, 0, sizeof g_v);
    memset(g_s, 0, sizeof g_s);
    g_v[0].x = 10; g_v[0].y = -35;          /* 5 below the lower joint */
    g_s[0].joint[0] = 5; g_s[0].weight[0] = 255;
    g_v[1].x = 0; g_v[1].y = 10;            /* on UP */
    g_s[1].joint[0] = 2; g_s[1].weight[0] = 255;
}

static struct sl_chr_tree g_t;
static void game_tree(void)
{
    memset(&g_t, 0, sizeof g_t);
    g_t.count = 5;
    g_t.parent[0] = -1; g_t.mtx[0] = 0;
    g_t.parent[1] = 0; g_t.origin[1][1] = 2; g_t.mtx[1] = 1;
    g_t.parent[2] = 0; g_t.origin[2][0] = 2; g_t.origin[2][1] = -2; g_t.mtx[2] = 2;
    g_t.parent[3] = 2; g_t.origin[3][0] = 4; g_t.mtx[3] = 3;       /* laid along +x */
    g_t.parent[4] = 0; g_t.origin[4][0] = -2; g_t.origin[4][1] = -2; g_t.mtx[4] = 4;
}

static void ident(float m[16]) { memset(m, 0, 64); m[0] = m[5] = m[10] = m[15] = 1.0f; }

int main(void)
{
    struct sl_chr_map map;
    char why[96];
    float game[5][16], jm[SL_CHR_MAX_JOINTS][16], eye[12];
    int k;

    fixture(); game_tree();
    ck(sl_chr_derive(&g_a, &g_t, &map, why) == 1 && map.ok,
       "a matching skeleton derives (%s)", why);
    ck(fabs(map.scale - 5.0f) < 1e-4f, "the scale is the bone-length ratio (%f)", map.scale);
    ck(map.mtx[0] == 0 && map.mtx[1] == 0 && map.mtx[2] == 1 && map.mtx[3] == 2
       && map.mtx[4] == 4 && map.mtx[5] == 3,
       "every item joint rides the game joint its bones match (root chain on the hub)");
    ck(map.rot[1] == 0, "the hub keeps its orientation");
    ck(map.rot[3] == 1, "the limb laid along +x is a quarter turn");
    ck(map.rot[5] == 1 && map.rot[2] == 0, "a joint with no child bone keeps its parent's turn");

    /* skinning through identity game matrices: the lower joint's frame */
    for (k = 0; k < 5; k++) ident(game[k]);
    sl_chr_joint_matrices(&map, (const float (*)[16]) game, 5, jm);
    sl_chr_skin_vertices(&g_a, (const float (*)[16]) jm, eye);
    ck(fabs(eye[0] - 1.0f) < 1e-5f && fabs(eye[1]) < 1e-5f && fabs(eye[2]) < 1e-5f,
       "5 below the item joint is 1 along the game part's axis (%f %f %f)",
       eye[0], eye[1], eye[2]);
    ck(fabs(eye[3]) < 1e-5f && fabs(eye[4]) < 1e-5f,
       "a vertex on its joint lands on the game joint's origin");
    /* the game matrix then applies: translate the lower part by (7, 0, 0) */
    game[3][12] = 7.0f;
    sl_chr_joint_matrices(&map, (const float (*)[16]) game, 5, jm);
    sl_chr_skin_vertices(&g_a, (const float (*)[16]) jm, eye);
    ck(fabs(eye[0] - 8.0f) < 1e-5f, "the game's own matrix places the part (%f)", eye[0]);
    /* a blended vertex is the weighted sum of its joints' results */
    g_s[1].joint[0] = 5; g_s[1].weight[0] = 128;
    g_s[1].joint[1] = 2; g_s[1].weight[1] = 127;
    g_v[1].x = 10; g_v[1].y = -35;
    sl_chr_skin_vertices(&g_a, (const float (*)[16]) jm, eye);
    {
        /* joint 5 alone -> (8,0,0); joint 2 alone -> T(-bind UP)/5 of (10,-35) = (2,-9) */
        float ex = (128.0f * 8.0f + 127.0f * 2.0f) / 255.0f;
        float ey = (127.0f * -9.0f) / 255.0f;
        ck(fabs(eye[3] - ex) < 1e-4f && fabs(eye[4] - ey) < 1e-4f,
           "two joints blend by weight (%f %f vs %f %f)", eye[3], eye[4], ex, ey);
    }

    /* refusals */
    fixture(); game_tree();
    g_t.origin[3][0] = 3.0f; g_t.origin[3][1] = 3.0f;       /* a bone no quarter turn aligns */
    ck(sl_chr_derive(&g_a, &g_t, &map, why) == 0 && !map.ok,
       "a bone no quarter turn aligns is refused (%s)", why);
    fixture(); game_tree();
    g_t.origin[3][0] = 6.0f;                                 /* off the common scale */
    ck(sl_chr_derive(&g_a, &g_t, &map, why) == 0, "a bone off the common scale is refused (%s)", why);
    fixture(); game_tree();
    g_t.count = 4;                                           /* no game joint for RIGHT */
    ck(sl_chr_derive(&g_a, &g_t, &map, why) == 0,
       "more item children than game children is refused (%s)", why);
    fixture(); game_tree();
    g_a.joint_count = 7;                                     /* a second, unmatched root */
    joint(6, "SKEL_LOOSE", -1, 0, 0, 0);
    ck(sl_chr_derive(&g_a, &g_t, &map, why) == 1 && map.mtx[6] == -1,
       "an unmatched joint no vertex uses is carried as unmatched (%s)", why);
    g_s[0].joint[0] = 6;
    ck(sl_chr_derive(&g_a, &g_t, &map, why) == 0,
       "a vertex bound to a joint the model lacks is refused (%s)", why);
    fixture(); game_tree();
    g_a.skin = NULL;
    ck(sl_chr_derive(&g_a, &g_t, &map, why) == 0, "an item with no skin is refused");

    /* the composition of a character's two parts */
    ck(sl_chr_compose(1, 1, 1) == (SL_CHR_DRAW_BODY | SL_CHR_DRAW_HEAD),
       "a replaced body and a replaced separate head both draw, the built-in head left out");
    ck(sl_chr_compose(1, 1, 0) == 0u,
       "a replaced body under the game's own separate head is not composed - the game's body keeps the neck");
    ck(sl_chr_compose(1, 0, 1) == SL_CHR_DRAW_HEAD,
       "a replaced separate head on the game's body draws alone");
    ck(sl_chr_compose(1, 0, 0) == 0u, "no item for either part: the game's character");
    ck(sl_chr_compose(0, 1, 0) == (SL_CHR_DRAW_BODY | SL_CHR_DRAW_BUILTIN_HEAD),
       "a body worn with no separate head draws its own built-in head");
    ck(sl_chr_compose(0, 0, 0) == 0u && sl_chr_compose(0, 0, 1) == 0u,
       "no separate head worn and no body item: nothing to draw");
    {
        int w, b, h, ok = 1;
        for (w = 0; w < 2; w++) for (b = 0; b < 2; b++) for (h = 0; h < 2; h++) {
            unsigned c = sl_chr_compose(w, b, h);
            if ((c & SL_CHR_DRAW_BODY) && !(c & (SL_CHR_DRAW_BUILTIN_HEAD | SL_CHR_DRAW_HEAD)))
                ok = 0;                                  /* a body with no neck owner */
            if ((c & SL_CHR_DRAW_BODY) && !b) ok = 0;    /* an item that is not there */
            if ((c & SL_CHR_DRAW_HEAD) && (!h || !w)) ok = 0;
            if ((c & SL_CHR_DRAW_BUILTIN_HEAD) && (w || !(c & SL_CHR_DRAW_BODY))) ok = 0;
        }
        ck(ok, "every composition that draws the body draws a neck owner, and only items that exist");
    }

    /* THE SCREEN'S PLACE: the game's screen onto the item's full-image quad */
    {
        static struct sl_wa_vertex sv[8];
        static unsigned si[12] = { 0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7 };
        struct sl_chr_map sm;
        float q[4][3] = { { -10, -8, 0 }, { 10, -8, 0 }, { 10, 8, 0 }, { -10, -8 + 16, 0 } };
        float o[4][3];
        int r;
        memset(&g_a, 0, sizeof g_a);
        memset(&sm, 0, sizeof sm);
        g_a.asset_class = SL_WA_CLASS_PROP;
        g_a.vertices = sv;
        g_a.indices = si;
        g_a.vertex_count = 8;
        g_a.index_count = 12;
        g_a.mesh_count = 1;
        g_a.meshes[0].first_index = 0;
        g_a.meshes[0].index_count = 12;
        sm.count = 1; sm.scale = 0.5f;               /* item = model * 0.5 + bind */
        sm.bind[0][0] = 100; sm.bind[0][1] = 50; sm.bind[0][2] = 7;
        q[3][0] = -10;
        memset(sv, 0, sizeof sv);
        /* a bezel quad far off the screen, and the screen quad: model x
         * -12..12, y -6..6 on the screen's plane, 0.2 behind it */
        sv[0].x = 200; sv[1].x = 210; sv[2].x = 210; sv[3].x = 200;
        sv[2].y = 10;  sv[3].y = 10;
        sv[0].u = 0; sv[0].v = 0; sv[1].u = 1; sv[1].v = 0; sv[2].u = 1; sv[2].v = 1; sv[3].u = 0; sv[3].v = 1;
        sv[4].x = 100 - 6; sv[4].y = 50 - 3; sv[5].x = 100 + 6; sv[5].y = 50 - 3;
        sv[6].x = 100 + 6; sv[6].y = 50 + 3; sv[7].x = 100 - 6; sv[7].y = 50 + 3;
        sv[4].z = sv[5].z = sv[6].z = sv[7].z = 7.0f - 0.1f;
        sv[4].u = 0.01f; sv[4].v = 0.99f; sv[5].u = 0.99f; sv[5].v = 1.0f;
        sv[6].u = 1.0f; sv[6].v = 0.0f; sv[7].u = 0.0f; sv[7].v = 0.01f;
        r = sl_screen_place(&g_a, &sm, (const float (*)[3]) q, o);
        ck(r == 1 && fabsf(o[0][0] + 12) < 1e-3f && fabsf(o[0][1] + 6) < 1e-3f
           && fabsf(o[2][0] - 12) < 1e-3f && fabsf(o[2][1] - 6) < 1e-3f
           && fabsf(o[1][0] - 12) < 1e-3f && fabsf(o[3][1] - 6) < 1e-3f
           && fabsf(o[0][2]) < 1e-5f && fabsf(o[2][2]) < 1e-5f,
           "the game's screen takes the item's full-image rectangle, in its own plane (%d: %.2f %.2f .. %.2f %.2f z %.3f)",
           r, (double) o[0][0], (double) o[0][1], (double) o[2][0], (double) o[2][1], (double) o[0][2]);
        /* the item's face tilted across the quad's plane: 0.3 in front of it
         * at the top, 0.3 behind at the bottom; the bezel behind both */
        sv[6].z = sv[7].z = 7.0f + 0.15f;
        sv[4].z = sv[5].z = 7.0f - 0.15f;
        r = sl_screen_place(&g_a, &sm, (const float (*)[3]) q, o);
        ck(r == 1 && o[2][2] == 1.0f && o[3][2] == 1.0f && o[0][2] == 0.0f && o[1][2] == 0.0f
           && o[0][0] == -12.0f && o[2][0] == 12.0f && o[0][1] == -6.0f && o[2][1] == 6.0f,
           "a corner the item's face stands in front of is set in front of it, in whole units (z %.2f %.2f %.2f %.2f)",
           (double) o[0][2], (double) o[1][2], (double) o[2][2], (double) o[3][2]);
        sv[0].z = sv[1].z = sv[2].z = sv[3].z = 14.0f;
        r = sl_screen_place(&g_a, &sm, (const float (*)[3]) q, o);
        ck(r == 1 && o[2][2] == 0.0f && o[3][2] == 0.0f && o[0][2] == -1.0f && o[1][2] == -1.0f,
           "the front is the side away from the item's body (z %.2f %.2f %.2f %.2f)",
           (double) o[0][2], (double) o[1][2], (double) o[2][2], (double) o[3][2]);
        sv[0].z = sv[1].z = sv[2].z = sv[3].z = 0.0f;
        sv[4].z = sv[5].z = sv[6].z = sv[7].z = 7.0f - 0.1f;
        sv[4].x = 100 - 5; sv[7].x = 100 - 5; sv[5].x = 100 + 5; sv[6].x = 100 + 5;
        sv[4].y = 50 - 4; sv[5].y = 50 - 4; sv[6].y = 50 + 4; sv[7].y = 50 + 4;
        ck(sl_screen_place(&g_a, &sm, (const float (*)[3]) q, o) == 0 && o[2][0] == 10.0f,
           "a screen already on the item's rectangle stays where the game put it");
        sv[4].x = 100 - 6; sv[7].x = 100 - 6; sv[5].x = 100 + 6; sv[6].x = 100 + 6;
        sv[4].z = sv[5].z = sv[6].z = sv[7].z = 7.0f - 3.0f;
        ck(sl_screen_place(&g_a, &sm, (const float (*)[3]) q, o) == 0,
           "a full-image quad off the screen's face is not its screen");
        sv[4].z = sv[5].z = sv[6].z = sv[7].z = 7.0f - 0.1f;
        sv[6].u = 0.5f;
        ck(sl_screen_place(&g_a, &sm, (const float (*)[3]) q, o) == 0,
           "a quad that does not lay the whole image is no screen (a flat or tiled surface)");
        sv[6].u = 1.0f;
        sv[4].x = 100 + 40; sv[7].x = 100 + 40; sv[5].x = 100 + 52; sv[6].x = 100 + 52;
        ck(sl_screen_place(&g_a, &sm, (const float (*)[3]) q, o) == 0,
           "a full-image quad beside the screen is not its screen");
        sm.rot[0] = 1;
        ck(sl_screen_place(&g_a, &sm, (const float (*)[3]) q, o) == 0, "a turned item is not placed");
        ck(sl_screen_place(NULL, &sm, (const float (*)[3]) q, o) == 0, "no item, no place");
    }

    /* THE PROP'S TWO PASSES: an item's draws by blend */
    {
        fixture();
        g_a.asset_class = SL_WA_CLASS_PROP;
        g_a.material_count = 2;
        g_a.materials[0].blend = SL_WA_BLEND_OPAQUE;
        g_a.materials[1].blend = SL_WA_BLEND_ALPHA;
        g_a.mesh_count = 1;
        g_a.meshes[0].material = 0;
        ck(sl_prop_item_passes(&g_a) == SL_CHR_PASS_SOLID, "a solid item draws in the solid walk only");
        g_a.mesh_count = 2;
        g_a.meshes[1].material = 1;
        ck(sl_prop_item_passes(&g_a) == (SL_CHR_PASS_SOLID | SL_CHR_PASS_TRANSLUCENT),
           "an item with glass draws in both walks");
        g_a.meshes[0].material = 1;
        ck(sl_prop_item_passes(&g_a) == SL_CHR_PASS_TRANSLUCENT, "an all-glass item draws in the later walk only");
        g_a.materials[1].blend = SL_WA_BLEND_CUTOUT;
        ck(sl_prop_item_passes(&g_a) == SL_CHR_PASS_SOLID, "a cutout draw is solid: it writes depth");
        ck(sl_prop_item_passes(NULL) == 0u, "no item, no pass");
        ck(sl_chr_pass_draws(0u, 0) && sl_chr_pass_draws(0u, 1), "a record with no pass draws every mesh");
        ck(sl_chr_pass_draws(SL_CHR_PASS_SOLID, 0) && !sl_chr_pass_draws(SL_CHR_PASS_SOLID, 1),
           "the solid walk's record draws the solid meshes only");
        ck(!sl_chr_pass_draws(SL_CHR_PASS_TRANSLUCENT, 0) && sl_chr_pass_draws(SL_CHR_PASS_TRANSLUCENT, 1),
           "the later walk's record draws the glass only");
        ck(sl_chr_pass_draws(SL_CHR_PASS_SOLID | SL_CHR_PASS_TRANSLUCENT, 0)
           && sl_chr_pass_draws(SL_CHR_PASS_SOLID | SL_CHR_PASS_TRANSLUCENT, 1),
           "a fading object's one record draws both");
    }

    /* a body item that carries its own head (SL_WA_MESH_OWN_HEAD) */
    {
        static struct sl_wa_mesh ms[2];
        fixture();
        g_a.asset_class = SL_WA_CLASS_CHAR;
        g_a.mesh_count = 2;
        memset(ms, 0, sizeof ms);
        memcpy(g_a.meshes, ms, sizeof ms);
        ck(!sl_chr_item_owns_head(&g_a), "a body item with no own-head mark takes a separate head");
        g_a.meshes[0].flags = g_a.meshes[1].flags = SL_WA_MESH_OWN_HEAD;
        ck(sl_chr_item_owns_head(&g_a), "a body item marked own-head stands for the worn head");
        g_a.asset_class = SL_WA_CLASS_HEAD;
        ck(!sl_chr_item_owns_head(&g_a), "a head item never owns a head of its own");
        ck(!sl_chr_item_owns_head(NULL), "no item owns nothing");
    }

    /* THE VIEW MODEL: item joints to the game's positioned parts, one scale */
    {
        struct sl_vm_parts pt;
        unsigned vis[2];
        /* item: TOP (root, at 5 x the game root), FINGER (part 6), SLIDE
         * (part 7), a leaf marker no vertex uses; game scale 5 */
        memset(&g_a, 0, sizeof g_a);
        g_a.asset_class = SL_WA_CLASS_GUN;
        g_a.joint_count = 4;
        joint(0, "SKEL_TOP", -1, 0, -50, -100);
        joint(1, "SKEL_FINGER", 0, -20, -40, -60);
        joint(2, "SKEL_SLIDE", 0, 0, 10, 20);
        joint(3, "BEAN_END", 1, -20, -40, -60);
        g_a.joints[1].part = 7;                 /* Switches[6] */
        g_a.joints[2].part = 8;                 /* Switches[7] */
        memset(g_v, 0, sizeof g_v); memset(g_s, 0, sizeof g_s);
        g_a.vertex_count = 2; g_a.vertices = g_v; g_a.skin = g_s;
        g_s[0].joint[0] = 0; g_s[0].weight[0] = 255;
        g_s[1].joint[0] = 1; g_s[1].weight[0] = 255;
        memset(&pt, 0, sizeof pt);
        pt.root_origin[1] = -10; pt.root_origin[2] = -20; pt.root_mtx = 0;
        pt.count = 36; pt.mtx_count = 6;
        pt.present[6] = 1; pt.origin[6][0] = -4; pt.origin[6][1] = -8; pt.origin[6][2] = -12; pt.mtx[6] = 4;
        pt.present[7] = 1; pt.origin[7][1] = 2; pt.origin[7][2] = 4; pt.mtx[7] = 5;
        ck(sl_vm_derive(&g_a, &pt, &map, why) == 1 && map.ok && fabs((double) map.scale - 5.0) < 1e-4
           && map.mtx[0] == 0 && map.mtx[1] == 4 && map.mtx[2] == 5 && map.mtx[3] == -1
           && map.rot[1] == 0, "a view model rides the game's parts at the measured scale (%s)", why);
        pt.origin[7][2] = 9;                    /* the slide not where the game's is */
        ck(sl_vm_derive(&g_a, &pt, &map, why) == 0,
           "a joint away from the game's part is refused (%s)", why);
        pt.origin[7][2] = 4; pt.present[6] = 0;
        ck(sl_vm_derive(&g_a, &pt, &map, why) == 0,
           "a part the model does not position is refused (%s)", why);
        pt.present[6] = 1;
        g_s[1].joint[0] = 3;                    /* a vertex on the leaf marker */
        ck(sl_vm_derive(&g_a, &pt, &map, why) == 0,
           "a vertex bound to no positioned part is refused (%s)", why);
        g_s[1].joint[0] = 1;

        /* a ROOT-ONLY item (a fist, a knife): related by the root alone */
        {
            struct sl_wa_joint keep[4];
            memcpy(keep, g_a.joints, sizeof keep);
            g_a.joints[1].part = 0; g_a.joints[2].part = 0;
            g_s[1].joint[0] = 0;
            ck(sl_vm_derive(&g_a, &pt, &map, why) == 1 && fabs((double) map.scale - 5.0) < 1e-4
               && map.mtx[0] == 0 && map.mtx[1] == -1,
               "an item whose only positioned joint is its root takes the scale from the root (%s)", why);
            g_a.joints[0].bind[0] = 40;           /* the root points elsewhere */
            ck(sl_vm_derive(&g_a, &pt, &map, why) == 0,
               "a root-only item whose root is not where the model's is, is refused (%s)", why);
            memcpy(g_a.joints, keep, sizeof keep);
            g_s[1].joint[0] = 1;
        }

        /* THE COSTUME: whether any draw stands under a cuff switch */
        {
            static struct sl_wa_mesh ms[2];
            memset(ms, 0, sizeof ms);
            g_a.meshes[0] = ms[0]; g_a.meshes[1] = ms[1];
            g_a.mesh_count = 2;
            ck(!sl_vm_item_gates(&g_a, SL_VM_CUFF_FIRST, SL_VM_CUFFS),
               "an item with no draw under a cuff switch gates no costume");
            g_a.meshes[1].flags = (unsigned) (SL_VM_CUFF_FIRST + 4 + 1) << 16;
            ck(sl_vm_item_gates(&g_a, SL_VM_CUFF_FIRST, SL_VM_CUFFS),
               "a draw under a cuff switch is a costume gate");
            g_a.meshes[1].flags = (unsigned) (11 + 1) << 8;
            ck(!sl_vm_item_gates(&g_a, SL_VM_CUFF_FIRST, SL_VM_CUFFS),
               "a hand switch is no costume gate");
            g_a.mesh_count = 0;
        }

        /* THE REFLECTION: the item's own directions, turned by the pose */
        {
            static unsigned ix[3] = { 0, 1, 2 };
            static struct sl_wa_vertex tv[3];
            static struct sl_wa_skin ts[3];
            float n[9], uv[6], jm[1][16];
            struct sl_wa_asset t;
            memset(&t, 0, sizeof t); memset(tv, 0, sizeof tv); memset(ts, 0, sizeof ts);
            tv[1].x = 1; tv[2].y = 1;             /* a triangle facing +z */
            t.vertex_count = 3; t.vertices = tv; t.index_count = 3; t.indices = ix;
            t.skin = ts;
            ts[0].weight[0] = ts[1].weight[0] = ts[2].weight[0] = 255;
            sl_vm_item_normals(&t, n);
            ck(fabs((double) n[2] - 1.0) < 1e-5 && fabs((double) n[8] - 1.0) < 1e-5,
               "a vertex's direction is its faces' (+z here)");
            memset(jm, 0, sizeof jm);
            jm[0][0] = 0.1f; jm[0][6] = 0.1f; jm[0][9] = -0.1f; jm[0][15] = 1.0f; /* z -> -y, scaled */
            sl_vm_sphere_coords(&t, (const float (*)[16]) jm, n, uv);
            ck(fabs((double) uv[0] - 0.5) < 1e-5 && fabs((double) uv[1] - 1.0) < 1e-5,
               "the reflection is read where the TURNED direction points, whatever the pose's "
               "scale (%.3f %.3f)", (double) uv[0], (double) uv[1]);
        }

        /* THE PROP: the item's reference box on the model's own */
        {
            struct sl_prop_box pb;
            static struct sl_wa_asset keep;
            keep = g_a;
            memset(&pb, 0, sizeof pb);
            g_a.asset_class = SL_WA_CLASS_PROP;
            g_a.joint_count = 1;
            g_a.joints[0].parent = -1;
            g_a.bounds_min[0] = -10; g_a.bounds_min[1] = 0;  g_a.bounds_min[2] = -20;
            g_a.bounds_max[0] = 10;  g_a.bounds_max[1] = 40; g_a.bounds_max[2] = 0;
            pb.lo[0] = -70; pb.lo[1] = 0;   pb.lo[2] = -140;   /* 7 x the item's */
            pb.hi[0] = 70;  pb.hi[1] = 280; pb.hi[2] = 0;
            pb.root_mtx = 0; pb.mtx_count = 1;
            pb.root_origin[1] = 70;                           /* the root 70 up */
            ck(sl_prop_derive(&g_a, &pb, &map, why) == 1 && fabs((double) map.scale * 7.0 - 1.0) < 1e-5
               && map.count == 1 && map.mtx[0] == 0 && fabs((double) map.bind[0][1] - 10.0) < 1e-4
               && fabs((double) map.bind[0][2] - 0.0) < 1e-4,
               "a prop rides the model at the scale its box and the model's agree on, its joint "
               "where the root sits (%s; bind y %.3f)", why, (double) map.bind[0][1]);
            pb.hi[1] = 200;                                   /* not the same shape */
            ck(sl_prop_derive(&g_a, &pb, &map, why) == 0,
               "an item whose box is not the model's is refused (%s)", why);
            /* THE FITTED ITEM (D-038): the item 20 x 40 x 20 into the model's
             * 140 x 400 x 140 - x and z the tightest axes, 1/7; centred in x
             * and z, its bottom on the model's bottom (root 70 up), not its
             * middle on the model's middle */
            pb.fit_ok = 1;
            pb.hi[1] = 400;
            ck(sl_prop_derive(&g_a, &pb, &map, why) == 1 && map.ok && map.rot[0] == 0
               && fabs((double) map.scale * 7.0 - 1.0) < 1e-5 && fabs((double) map.bind[0][0]) < 1e-4
               && fabs((double) map.bind[0][1] - 10.0) < 1e-4
               && fabs((double) map.bind[0][2]) < 1e-4,
               "a fitted item keeps its proportions inside the model's box, seated on its bottom "
               "(%s; scale %.4f bind %.3f %.3f %.3f)", why, (double) map.scale,
               (double) map.bind[0][0], (double) map.bind[0][1], (double) map.bind[0][2]);
            pb.fit_ok = 0;
            pb.hi[1] = 280; pb.root_mtx = 3;
            ck(sl_prop_derive(&g_a, &pb, &map, why) == 0, "a model with no such root matrix is refused");
            pb.root_mtx = 0; g_a.asset_class = SL_WA_CLASS_GUN; g_a.joints[0].part = 0;
            ck(sl_vm_item_static(&g_a) && sl_prop_derive(&g_a, &pb, &map, why) == 1,
               "a static view model (one rigid joint) is placed by its box as a prop is (%s)", why);
            g_a.joint_count = 2;
            ck(!sl_vm_item_static(&g_a) && sl_prop_derive(&g_a, &pb, &map, why) == 0,
               "a view model with a skeleton is not placed by a box");
            g_a.joint_count = 1; g_a.asset_class = SL_WA_CLASS_CHAR;
            ck(sl_prop_derive(&g_a, &pb, &map, why) == 0, "a character item is no prop");
            /* THE TURNED ITEM: the item's y along the model's x, both drawn
             * from the grip at their origins */
            {
                float gm[16], jm[1][16], p[3];
                g_a.asset_class = SL_WA_CLASS_PROP; g_a.joint_count = 1; g_a.joints[0].part = 0;
                g_a.bounds_min[0] = -10; g_a.bounds_min[1] = 10;  g_a.bounds_min[2] = -20;
                g_a.bounds_max[0] = 10;  g_a.bounds_max[1] = 210; g_a.bounds_max[2] = 20;
                memset(&pb, 0, sizeof pb);
                pb.lo[0] = -105; pb.lo[1] = -5; pb.lo[2] = -10;   /* half the item's, turned, */
                pb.hi[0] = -5;   pb.hi[1] = 5;  pb.hi[2] = 10;    /* along -x from the grip  */
                pb.root_mtx = 0; pb.mtx_count = 1;
                ck(sl_prop_derive(&g_a, &pb, &map, why) == 0,
                   "a box that only fits turned is refused where no turn may be asked (%s)", why);
                pb.turn_ok = 1;
                ck(sl_prop_derive(&g_a, &pb, &map, why) == 1 && map.ok && map.rot[0] == 1
                   && fabs((double) map.scale - 2.0) < 1e-5,
                   "a box the model's under a quarter turn takes the turn that lays it out from "
                   "the grip as the model's lies (%s rot %d)", why, map.rot[0]);
                g_a.bounds_min[0] = -60; g_a.bounds_max[0] = -40;  /* the wrist off the grip */
                pb.lo[1] = 20; pb.hi[1] = 30;
                ck(sl_prop_derive(&g_a, &pb, &map, why) == 1 && map.rot[0] == 1,
                   "a wrist offset across the long axis does not decide the turn (%s)", why);
                g_a.bounds_min[0] = -10; g_a.bounds_max[0] = 10;
                pb.lo[1] = -5; pb.hi[1] = 5;
                (void) sl_prop_derive(&g_a, &pb, &map, why);
                memset(gm, 0, sizeof gm);
                gm[0] = gm[5] = gm[10] = gm[15] = 1.0f;
                sl_chr_joint_matrices(&map, (const float (*)[16]) gm, 1, jm);
                p[0] = 0.0f * jm[0][0] + 210.0f * jm[0][4] + jm[0][12];
                p[1] = 0.0f * jm[0][1] + 210.0f * jm[0][5] + jm[0][13];
                ck(fabs((double) p[0] + 105.0) < 1e-3 && fabs((double) p[1]) < 1e-3,
                   "the turned item's far end lands on the model's (%.3f %.3f)",
                   (double) p[0], (double) p[1]);
                pb.lo[0] = 5; pb.hi[0] = 105;
                ck(sl_prop_derive(&g_a, &pb, &map, why) == 1 && map.rot[0] == 3,
                   "a model lying the other way from its grip takes the other turn");
                pb.lo[0] = -50; pb.hi[0] = 50;
                ck(sl_prop_derive(&g_a, &pb, &map, why) == 0,
                   "a model centred on its grip does not choose; the item is refused (%s)", why);
            }
            g_a = keep;
        }

        /* THE FIXED PARTS (#92): a weapon model's two GROUPSIMPLE matrices */
        ck(sl_prop_parts_fixed(2, 0, 2),
           "a model whose two matrices are both GROUPSIMPLE offsets is one rigid piece");
        ck(!sl_prop_parts_fixed(5, 5, 0) && !sl_prop_parts_fixed(2, 1, 1)
           && !sl_prop_parts_fixed(3, 0, 2),
           "a GROUP-moved model (the jeep), a mixed one, and one with a matrix no "
           "GROUPSIMPLE sets are not");
        ck(!sl_prop_parts_fixed(1, 0, 1) && !sl_prop_parts_fixed(0, 0, 0),
           "a one-matrix model is not this case (it is taken as it always was)");

        /* WHICH OBJECTS: the multi-screen monitor keeps each of its screens */
        ck(sl_prop_takes(SL_PROP_KIND_PROP, 0) == 0 && sl_prop_takes(SL_PROP_KIND_VEHICLE, 5) == 0,
           "a standard prop and a vehicle are taken with no screen kept");
        ck(sl_prop_takes(SL_PROP_KIND_MONITOR, 1) == 1 && sl_prop_takes(SL_PROP_KIND_MONITOR, 0) == 0,
           "a single monitor keeps its one screen drawing over the item");
        ck(sl_prop_takes(SL_PROP_KIND_MULTI_MONITOR, 4) == 4
           && sl_prop_takes(SL_PROP_KIND_MULTI_MONITOR, 6) == SL_PROP_MAX_SCREENS
           && sl_prop_takes(SL_PROP_KIND_MULTI_MONITOR, 2) == 2,
           "a multi-screen monitor keeps each of its screens, at most four");
        ck(sl_prop_takes(SL_PROP_KIND_GAS, 0) == 0,
           "a gas prop is taken with no screen kept");
        ck(sl_prop_takes(SL_PROP_KIND_ARMOUR, 0) == 0 && sl_prop_takes(SL_PROP_KIND_HAT, 0) == 0,
           "body armour and a dropped hat are taken with no screen kept");
        ck(sl_prop_takes(SL_PROP_KIND_AMMO, 0) == 0 && sl_prop_takes(SL_PROP_KIND_AUTOGUN, 5) == 0
           && sl_prop_takes(SL_PROP_KIND_AIRCRAFT, 3) == 0 && sl_prop_takes(SL_PROP_KIND_TANK, 5) == 0
           && SL_PROP_KIND_AMMO == 20 && SL_PROP_KIND_AUTOGUN == 13
           && SL_PROP_KIND_AIRCRAFT == 40 && SL_PROP_KIND_TANK == 45,
           "an ammo box, an autogun, an aircraft and a tank are taken, with no screen kept");
        ck(sl_prop_takes(SL_PROP_KIND_KEY, 0) == 0 && sl_prop_takes(SL_PROP_KIND_ALARM, 0) == 0
           && sl_prop_takes(SL_PROP_KIND_COLLECTABLE, 3) == 0
           && SL_PROP_KIND_KEY == 4 && SL_PROP_KIND_ALARM == 5 && SL_PROP_KIND_COLLECTABLE == 8
           && sl_prop_keeps_gunfire(SL_PROP_KIND_COLLECTABLE) && !sl_prop_keeps_gunfire(SL_PROP_KIND_PROP)
           && !sl_prop_keeps_gunfire(SL_PROP_KIND_KEY),
           "keys, alarms and collectables are one-piece props; a collectable keeps the game's "
           "muzzle flash (#80)");
        ck(sl_prop_takes(SL_PROP_KIND_GLASS, 0) == 0,
           "breakable glass is taken with no screen kept");
        ck(sl_prop_takes(1, 0) == -1 && sl_prop_takes(47, 0) == -1
           && sl_prop_takes(6, 0) == -1 && sl_prop_takes(9, 0) == -1 && sl_prop_takes(0, 0) == -1,
           "a door, tinted glass, a camera, a guard and every other kind are not taken as a "
           "rigid prop");

        /* THE WORN HAT'S SEAT: the game's node x T(21.3 x offset) x S(size),
         * composed as chr.c composes it, comes back to the node's own matrix */
        {
            const float node[16] = { 0.0f, 0.8f, -0.6f, 0.0f,   0.0f, 0.6f, 0.8f, 0.0f,
                                     1.0f, 0.0f, 0.0f, 0.0f,    310.0f, -42.5f, 977.0f, 1.0f };
            const float seat[6] = { 0.237f, 0.977f, -0.44f, 1.178463f, 1.14061f, 1.143465f };
            const float none[6] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
            float g[16], keep[16], e = 0.0f, pt[3] = { 12.0f, 200.0f, -7.0f }, pa[3], pb[3];
            int i, c;
            /* row-major, v * m: rows 0..2 of the node scaled by the size, row 3
             * moved by the offset taken through the node */
            for (i = 0; i < 3; i++)
                for (c = 0; c < 4; c++) g[4 * i + c] = seat[3 + i] * node[4 * i + c];
            for (c = 0; c < 4; c++)
                g[12 + c] = node[12 + c] + SL_HAT_SEAT_UNIT * (seat[0] * node[c] + seat[1] * node[4 + c]
                                                              + seat[2] * node[8 + c]);
            /* the same point placed by the entry first and the node after */
            for (c = 0; c < 3; c++) {
                float v[3];
                for (i = 0; i < 3; i++) v[i] = seat[3 + i] * pt[i] + SL_HAT_SEAT_UNIT * seat[i];
                pa[c] = v[0] * node[c] + v[1] * node[4 + c] + v[2] * node[8 + c] + node[12 + c];
                pb[c] = pt[0] * g[c] + pt[1] * g[4 + c] + pt[2] * g[8 + c] + g[12 + c];
            }
            ck(fabsf(pa[0] - pb[0]) + fabsf(pa[1] - pb[1]) + fabsf(pa[2] - pb[2]) < 1e-2f,
               "the composed matrix places a point as the entry then the node do");
            memcpy(keep, g, sizeof keep);
            ck(sl_hat_unseat(g, seat, 0.0f) == 1, "a recorded entry is taken back");
            for (i = 0; i < 16; i++) e += fabsf(g[i] - node[i]);
            ck(e < 1e-3f, "taking the entry back leaves the node's own matrix (error %g)", (double) e);
            memcpy(g, keep, sizeof keep);
            ck(sl_hat_unseat(g, seat, 7.0f) == 1, "a recorded entry is taken back with a rest");
            e = 0.0f;
            for (c = 0; c < 4; c++) e += fabsf(g[12 + c] - (node[12 + c] + 7.0f * node[4 + c]));
            ck(e < 1e-3f, "the rest raises the hat along the node's own y (error %g)", (double) e);
            memcpy(keep, g, sizeof keep);
            ck(sl_hat_unseat(g, none, 7.0f) == 0 && memcmp(g, keep, sizeof keep) == 0,
               "no recorded entry leaves the matrix alone");
        }

        /* THE HAT'S REST: a flat head top under a flat cap, in item units at
         * scale 4 about a bind of (0, 100, 0) */
        {
            static struct sl_wa_vertex hv[4], cv[4];
            static const unsigned quad[6] = { 0, 1, 2, 0, 2, 3 };
            struct sl_wa_asset ha, ca;
            const float hb[3] = { 0.0f, 100.0f, 0.0f }, cb[3] = { 0.0f, 100.0f, 0.0f };
            const float org[3] = { 0.0f, 0.0f, 0.0f }, up[3] = { 0.0f, 30.0f, 0.0f };
            int k;
            for (k = 0; k < 4; k++) {
                float x = (k == 1 || k == 2) ? 80.0f : -80.0f, z = (k >= 2) ? 80.0f : -80.0f;
                memset(&hv[k], 0, sizeof hv[k]);
                hv[k].x = x; hv[k].z = z; hv[k].y = 100.0f + 4.0f * 10.0f;   /* head top at 10 */
                cv[k] = hv[k];
                cv[k].x *= 0.5f; cv[k].z *= 0.5f; cv[k].y = 100.0f + 4.0f * 4.0f;  /* cap at 4 */
            }
            memset(&ha, 0, sizeof ha);
            memset(&ca, 0, sizeof ca);
            ha.vertices = hv; ha.vertex_count = 4; ha.indices = quad; ha.index_count = 6;
            ca.vertices = cv; ca.vertex_count = 4; ca.indices = quad; ca.index_count = 6;
            ck(fabsf(sl_hat_rest(&ha, hb, 4.0f, &ca, cb, 4.0f, org) - 6.0f) <= 0.25f,
               "a head 6 units above the cap raises the cap by 6 (%g)",
               (double) sl_hat_rest(&ha, hb, 4.0f, &ca, cb, 4.0f, org));
            ck(sl_hat_rest(&ha, hb, 4.0f, &ca, cb, 4.0f, up) == 0.0f,
               "a cap already above the head stays where the node puts it");
            ca.index_count = 0;
            ck(sl_hat_rest(&ha, hb, 4.0f, &ca, cb, 4.0f, org) == 0.0f,
               "a cap with no geometry over the head is not raised");
        }

        /* THE WEAPON ON THE WATCH: the bridge's look from the game's render data */
        {
            unsigned char sh[4];
            int fd = -1;
            sl_vm_menu_look(4, 0x64DC6428u, 0x11223344u, sh, &fd);
            ck(sh[0] == 0x64 && sh[1] == 0xDC && sh[2] == 0x64 && sh[3] == 0x28 && fd == 255,
               "the mission page's weapon: the first-person look, fog from envcolour, opaque");
            sl_vm_menu_look(SL_VM_LOOK_TRANSLUCENT_TYPE, 0x00000040u, 0xA0FFA03Cu, sh, &fd);
            ck(sh[0] == 0xA0 && sh[1] == 0xFF && sh[2] == 0xA0 && sh[3] == 0x3C && fd == 0x40,
               "the equipment page's weapon: fog from fogcolour, translucent by envcolour's alpha");
            sl_vm_menu_look(SL_VM_LOOK_TRANSLUCENT_TYPE, 0x000000FFu, 0xA0FFA03Cu, sh, &fd);
            ck(fd == 255, "a translucent mode at full alpha is opaque");
        }

        /* THE DYNAMIC DOOR: which doors, which half-space, and the cut */
        {
            static struct sl_wa_asset da;
            static struct sl_wa_vertex dv[8], ov[64];
            static struct sl_wa_skin dsk[8], osk[64];
            static const unsigned dix[12] = { 0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7 };
            static unsigned oi[64];
            struct sl_wa_mesh om[2];
            struct sl_door_cut cut;
            struct sl_chr_map dm;
            float box[6] = { -10, 10, 0, 10, 0, 2 };
            unsigned nv = 0, ni = 0, i2;
            double face, cap;
            int okc, allin;

            ck(sl_door_takes(0xc, 0) && sl_door_takes(0x4, 4) && !sl_door_takes(0x8, 0)
               && !sl_door_takes(0x4, 5) && !sl_door_takes(0x0, 4),
               "a door is dynamic for CLIP_TO_BBOX on a sliding or vertical door only");
            ck(sl_door_rides(0x0, 0) && sl_door_rides(0x8, 4) && !sl_door_rides(0x4, 0)
               && !sl_door_rides(0x4, 4) && sl_door_rides(0x0, 5) && sl_door_rides(0x2, 5)
               && !sl_door_rides(0x4, 5) && !sl_door_rides(0x0, 6) && !sl_door_rides(0x0, 1),
               "a door rides its matrix whole without CLIP_TO_BBOX: sliding, vertical or swinging");
            ck(sl_door_rides(0x0, 9) && !sl_door_rides(0x4, 9) && !sl_door_takes(0x0, 9)
               && !sl_door_rides(0x0, 8) && !sl_door_rides(0x0, 7),
               "#96 an Aztec chair rides its matrix whole as a swinging door does; a fall-away "
               "door is not taken");
            ck(!(sl_door_takes(0x4, 0) && sl_door_rides(0x4, 0))
               && !(sl_door_takes(0x0, 0) || !sl_door_rides(0x0, 0)),
               "a sliding door is either cut at its box or rides its matrix, never both");
            ck(sl_door_parts(0x0, 6) && sl_door_parts(0x8, 7) && !sl_door_parts(0x4, 6)
               && !sl_door_parts(0x4, 7) && !sl_door_parts(0x0, 0) && !sl_door_parts(0x0, 4)
               && !sl_door_parts(0x0, 5) && !sl_door_parts(0x0, 8) && !sl_door_parts(0x0, 9),
               "a door rides its moving parts without CLIP_TO_BBOX: eye or iris only");
            {
                int t, f, one = 1;
                for (t = 0; t <= 9; t++)
                    for (f = 0; f <= 0xf; f++)
                        if (sl_door_takes(f, t) + sl_door_rides(f, t) + sl_door_parts(f, t) > 1)
                            one = 0;
                ck(one, "a door is taken one way at most: cut, ridden whole, or ridden by its parts");
            }
            ck(sl_door_cut_of(0, box, &cut) && cut.on && cut.axis == 0 && cut.sign == 1
               && cut.value == -10.0f,
               "a sliding door keeps x at or above its live box's xmin");
            ck(sl_door_cut_of(4, box, &cut) && cut.axis == 1 && cut.sign == -1 && cut.value == 10.0f,
               "a vertical door keeps y at or below its live box's ymax");
            ck(!sl_door_cut_of(5, box, &cut) && !cut.on, "a swinging door has no cut");
            {
                /* the presentation cycle ORIGINAL -> COMMUNITY HD -> XBLA -> ORIGINAL:
                 * only XBLA carries the gate's item */
                static const int pres_item[4] = { 0, 0, 1, 0 };
                static const int want[4] = { SL_DOOR_PATH_NATIVE, SL_DOOR_PATH_NATIVE,
                                             SL_DOOR_PATH_ITEM, SL_DOOR_PATH_NATIVE };
                int s, okp = 1;
                for (s = 0; s < 4; s++)
                    if (sl_door_path(1, 1, pres_item[s]) != want[s]) okp = 0;
                ck(okp, "a live presentation cycle moves the door between its own lists and "
                        "the item at the next draw, from the inputs alone");
                ck(sl_door_path(0, 1, 1) == SL_DOOR_PATH_GAME
                   && sl_door_path(1, 0, 1) == SL_DOOR_PATH_GAME,
                   "SL_DYNDOOR=0, or a door the contract does not take, draws as the cartridge does");
            }

            /* a face (z 0, x -10..10, y 0..10, u along x) and its far end cap (x -10) */
            memset(&da, 0, sizeof da);
            da.asset_class = SL_WA_CLASS_PROP;
            da.joint_count = 1; da.joints[0].parent = -1;
            for (i2 = 0; i2 < 8; i2++) { memset(&dv[i2], 0, sizeof dv[i2]); dsk[i2].joint[0] = 0; dsk[i2].weight[0] = 255; }
            dv[0].x = -10; dv[0].y = 0;  dv[0].u = 0.0f; dv[0].v = 0.0f;
            dv[1].x =  10; dv[1].y = 0;  dv[1].u = 1.0f; dv[1].v = 0.0f;
            dv[2].x =  10; dv[2].y = 10; dv[2].u = 1.0f; dv[2].v = 1.0f;
            dv[3].x = -10; dv[3].y = 10; dv[3].u = 0.0f; dv[3].v = 1.0f;
            dv[4].x = -10; dv[4].y = 0;  dv[4].z = 0; dv[4].u = 0.25f;
            dv[5].x = -10; dv[5].y = 0;  dv[5].z = 2; dv[5].u = 0.5f;
            dv[6].x = -10; dv[6].y = 10; dv[6].z = 2; dv[6].u = 0.5f;
            dv[7].x = -10; dv[7].y = 10; dv[7].z = 0; dv[7].u = 0.25f;
            da.vertices = dv; da.skin = dsk; da.indices = dix;
            da.vertex_count = 8; da.index_count = 12; da.mesh_count = 2;
            da.meshes[0].first_index = 0; da.meshes[0].index_count = 6;
            da.meshes[1].first_index = 6; da.meshes[1].index_count = 6; da.meshes[1].material = 1;
            memset(&dm, 0, sizeof dm);
            dm.ok = 1; dm.count = 1; dm.scale = 1.0f;
            ck(sl_door_item_fits(&da), "a small rigid prop item can be cut");

            /* closed: the live box is the model's own - nothing changes */
            sl_door_cut_of(0, box, &cut);
            okc = sl_door_cut_mesh(&da, &dm, &cut, ov, osk, 64, oi, 64, om, &nv, &ni);
            ck(okc && nv == 8 && ni == 12 && memcmp(oi, dix, sizeof dix) == 0,
               "a closed door's item is drawn exactly as it is");

            /* 60% open: xmin at 2 */
            box[0] = 2.0f;
            sl_door_cut_of(0, box, &cut);
            okc = sl_door_cut_mesh(&da, &dm, &cut, ov, osk, 64, oi, 64, om, &nv, &ni);
            face = cap = 0.0; allin = 1;
            for (i2 = 0; okc && i2 < ni; i2 += 3) {
                const struct sl_wa_vertex *p = &ov[oi[i2]], *q = &ov[oi[i2 + 1]], *r = &ov[oi[i2 + 2]];
                double a2 = sqrt(door_area2(p, q, r)) * 0.5;
                if (p->x < 2.0f - 1e-4f || q->x < 2.0f - 1e-4f || r->x < 2.0f - 1e-4f) allin = 0;
                if (i2 < om[0].index_count) face += a2; else cap += a2;
            }
            ck(okc && allin && fabs(face - 80.0) < 1e-3 && fabs(cap - 20.0) < 1e-3
               && om[0].first_index == 0 && om[1].first_index == om[0].index_count
               && om[1].material == 1 && om[0].index_count + om[1].index_count == ni,
               "a sliding door's item keeps only what its box keeps (face %.3f of 80, "
               "the far cap %.3f of 20 laid on the face)", face, cap);
            for (i2 = 0, allin = 1; okc && i2 < om[0].index_count; i2++) {
                const struct sl_wa_vertex *p = &ov[oi[i2]];
                if (fabs((double) p->u - ((double) p->x + 10.0) / 20.0) > 1e-5) allin = 0;
            }
            ck(okc && allin, "the texture coordinate stays where it was on the surface it is cut from");
            for (i2 = om[1].first_index, allin = 1; okc && i2 < ni; i2++)
                if (fabs((double) ov[oi[i2]].x - 2.0) > 1e-5
                    || !(ov[oi[i2]].u == 0.25f || ov[oi[i2]].u == 0.5f)) allin = 0;
            ck(okc && allin, "the far cap rides the moving face with its own texture coordinates");

            /* vertical, 60% open: ymax at 4 */
            box[0] = -10.0f; box[3] = 4.0f;
            sl_door_cut_of(4, box, &cut);
            okc = sl_door_cut_mesh(&da, &dm, &cut, ov, osk, 64, oi, 64, om, &nv, &ni);
            face = cap = 0.0; allin = 1;
            for (i2 = 0; okc && i2 < ni; i2 += 3) {
                const struct sl_wa_vertex *p = &ov[oi[i2]], *q = &ov[oi[i2 + 1]], *r = &ov[oi[i2 + 2]];
                double a2 = sqrt(door_area2(p, q, r)) * 0.5;
                if (p->y > 4.0f + 1e-4f || q->y > 4.0f + 1e-4f || r->y > 4.0f + 1e-4f) allin = 0;
                if (i2 < om[0].index_count) face += a2; else cap += a2;
            }
            ck(okc && allin && fabs(face - 80.0) < 1e-3 && fabs(cap - 8.0) < 1e-3,
               "a vertical door's item keeps what lies at or below its box's top (%.3f, %.3f)",
               face, cap);

            /* the item's own frame: a quarter turn and a scale are undone to find the model */
            dm.scale = 2.0f; dm.rot[0] = 1; dm.bind[0][1] = 4.0f;   /* model x = 2 - y / 2 */
            box[3] = 10.0f; box[0] = 0.0f;
            sl_door_cut_of(0, box, &cut);
            okc = sl_door_cut_mesh(&da, &dm, &cut, ov, osk, 64, oi, 64, om, &nv, &ni);
            {
                float gm[1][16], jm2[1][16];
                static float me[64 * 3];
                struct sl_wa_asset tmp = da;
                ident(gm[0]);
                sl_chr_joint_matrices(&dm, (const float (*)[16]) gm, 1, jm2);
                tmp.vertices = ov; tmp.skin = osk; tmp.vertex_count = nv;
                sl_chr_skin_vertices(&tmp, (const float (*)[16]) jm2, me);
                for (i2 = 0, allin = 1; okc && i2 < ni; i2++)
                    if (me[3 * oi[i2]] < -1e-4f) allin = 0;
                ck(okc && allin && ni > 0,
                   "the cut is made in MODEL space, through the item's turn and scale");
            }
            dm.scale = 1.0f; dm.rot[0] = 0; dm.bind[0][1] = 0.0f;
            ck(!sl_door_cut_mesh(&da, &dm, &cut, ov, osk, 10, oi, 64, om, &nv, &ni)
               && !sl_door_cut_mesh(&da, &dm, &cut, ov, osk, 64, oi, 20, om, &nv, &ni),
               "a cut that could outgrow its buffers is refused before it starts");
            dm.count = 2;
            ck(!sl_door_cut_mesh(&da, &dm, &cut, ov, osk, 64, oi, 64, om, &nv, &ni),
               "an item that is not one rigid piece is not cut");
        }

        /* THE MOVING PROP: joints named for the model's matrices (a wheel) */
        {
            struct sl_prop_box pb;
            static struct sl_wa_asset keep;
            static struct sl_wa_skin keep_s[4];
            keep = g_a;
            memcpy(keep_s, g_s, sizeof keep_s);
            memset(&pb, 0, sizeof pb);
            memset(&g_a, 0, sizeof g_a);
            g_a.asset_class = SL_WA_CLASS_PROP;
            g_a.vertex_count = 4; g_a.vertices = g_v; g_a.skin = g_s;
            g_a.bounds_min[0] = -10; g_a.bounds_min[1] = 0;  g_a.bounds_min[2] = -20;
            g_a.bounds_max[0] = 10;  g_a.bounds_max[1] = 40; g_a.bounds_max[2] = 0;
            pb.lo[0] = -70; pb.lo[1] = 0;   pb.lo[2] = -140;  /* 7 x the item's */
            pb.hi[0] = 70;  pb.hi[1] = 280; pb.hi[2] = 0;
            pb.root_mtx = 0; pb.mtx_count = 3;
            pb.root_origin[1] = 70;
            pb.node_present[0] = 1; pb.node_origin[0][1] = 70;
            /* the model's wheel (matrix 2) at (-63, 14, -35) in model space */
            pb.node_present[2] = 1;
            pb.node_origin[2][0] = -63; pb.node_origin[2][1] = 14; pb.node_origin[2][2] = -35;
            joint(0, "BASE", -1, 0, 10, 0);
            joint(1, "WHEEL1", 0, -9, 2, -5);               /* the item's own pivot */
            joint(2, "BEAN_END1", 1, -12, 2, -5);           /* a leaf under the wheel */
            g_a.joint_count = 3;
            g_a.joints[0].part = SL_WA_PART_MATRIX + 1u;     /* base -> matrix 0 */
            g_a.joints[1].part = SL_WA_PART_MATRIX + 3u;     /* wheel1 -> matrix 2 */
            g_a.joints[2].part = SL_WA_PART_MATRIX + 3u;     /* rides its parent's */
            memset(g_s, 0, sizeof g_s);
            g_s[0].weight[0] = g_s[1].weight[0] = 255;
            g_s[2].joint[0] = 1; g_s[2].weight[0] = 255;
            g_s[3].joint[0] = 1; g_s[3].weight[0] = 255;
            ck(sl_prop_item_moves(&g_a), "a prop item whose joints name matrices moves");
            ck(sl_prop_derive(&g_a, &pb, &map, why) == 1 && map.count == 3
               && map.mtx[0] == 0 && map.mtx[1] == 2 && map.mtx[2] == 2
               && fabs((double) map.bind[1][0] - -9.0) < 1e-4
               && fabs((double) map.bind[1][1] - 2.0) < 1e-4
               && fabs((double) map.bind[1][2] - -5.0) < 1e-4,
               "each part rides its matrix, its joint at the model's node for it (%s; wheel "
               "bind %.3f %.3f %.3f)", why, (double) map.bind[1][0], (double) map.bind[1][1],
               (double) map.bind[1][2]);
            g_a.joints[1].bind[0] = -7;                     /* the source's own pivot a */
            ck(sl_prop_derive(&g_a, &pb, &map, why) == 1     /* little off the node: the */
               && fabs((double) map.bind[1][0] - -9.0) < 1e-4, /* game's pivot is kept */
               "a part's pivot is the game's node, not the item's joint (%s)", why);
            g_a.joints[1].bind[0] = 9;                      /* the far side of the model */
            ck(sl_prop_derive(&g_a, &pb, &map, why) == 0,
               "a joint named for a part the model places elsewhere is refused (%s)", why);
            joint(1, "WHEEL1", 0, 0, 10, 0);                /* on its parent's pivot */
            ck(sl_prop_derive(&g_a, &pb, &map, why) == 1 && map.mtx[1] == map.mtx[0]
               && fabs((double) map.bind[1][0] - map.bind[0][0]) < 1e-4
               && fabs((double) map.bind[1][1] - map.bind[0][1]) < 1e-4
               && fabs((double) map.bind[1][2] - map.bind[0][2]) < 1e-4
               && map.mtx[2] == 2,
               "a joint with no pivot of its own rides its parent's matrix (%s; mtx %d)",
               why, map.mtx[1]);
            joint(1, "WHEEL1", 0, -9, 2, -5);
            g_a.joints[1].part = SL_WA_PART_MATRIX + 3u;
            g_a.joints[1].bind[0] = -9;
            g_a.joints[1].part = SL_WA_PART_MATRIX + 2u;     /* matrix 1: no node */
            ck(sl_prop_derive(&g_a, &pb, &map, why) == 0,
               "a joint naming a matrix the model positions nothing under is refused (%s)", why);
            g_a.joints[1].part = SL_WA_PART_MATRIX + 3u;
            g_a.joints[0].part = 0;                         /* an unnamed root */
            ck(sl_prop_derive(&g_a, &pb, &map, why) == 1 && map.mtx[0] == 0
               && fabs((double) map.bind[0][1] - 10.0) < 1e-4,
               "an unnamed root rides the model's root (%s)", why);
            g_a.joints[1].part = 0; g_a.joints[2].part = 0;
            ck(!sl_prop_item_moves(&g_a) && sl_prop_derive(&g_a, &pb, &map, why) == 0,
               "several joints naming no matrix are no prop (%s)", why);

            /* #94 THE PARTS BY PLACE: two wheels the model builds alike, at
             * (-63, 14, -35) (matrix 2) and (-63, 14, 35) (matrix 1); the
             * item's names trade them - WHEEL1 named for matrix 2 sits on
             * matrix 1's node, WHEEL2 named for none sits on matrix 2's */
            pb.node_present[1] = 1;
            pb.node_origin[1][0] = -63; pb.node_origin[1][1] = 14; pb.node_origin[1][2] = 35;
            g_a.bounds_max[2] = 20;                          /* the item spans both */
            pb.lo[2] = -140; pb.hi[2] = 140;
            joint(0, "BASE", -1, 0, 10, 0);
            joint(1, "WHEEL1", 0, -9, 2, 5);
            joint(2, "BEAN_END1", 1, -9, 2, 5);
            joint(3, "WHEEL2", 0, -9, 2, -5);
            g_a.joint_count = 4;
            g_a.joints[0].part = SL_WA_PART_MATRIX + 1u;     /* matrix 0 */
            g_a.joints[1].part = SL_WA_PART_MATRIX + 3u;     /* named for matrix 2 */
            g_a.joints[2].part = SL_WA_PART_MATRIX + 3u;     /* its parent's */
            g_a.joints[3].part = SL_WA_PART_MATRIX + 1u;     /* named for none: the root's */
            ck(sl_prop_derive(&g_a, &pb, &map, why) == 1 && map.count == 4
               && map.mtx[0] == 0 && map.mtx[1] == 1 && map.mtx[2] == 1 && map.mtx[3] == 2
               && fabs((double) map.bind[1][2] - 5.0) < 1e-4
               && fabs((double) map.bind[3][2] - -5.0) < 1e-4,
               "parts whose names trade the model's nodes ride the node each sits on (%s; "
               "mtx %d %d %d %d)", why, map.mtx[0], map.mtx[1], map.mtx[2], map.mtx[3]);
            g_a.joints[2].bind[0] = -12;                     /* a leaf off every node */
            ck(sl_prop_derive(&g_a, &pb, &map, why) == 0,
               "one joint that sits on no node keeps the refusal (%s)", why);
            g_a.joints[2].bind[0] = -9;
            pb.node_present[1] = 0;                          /* nothing at WHEEL1's place */
            ck(sl_prop_derive(&g_a, &pb, &map, why) == 0,
               "a joint on no present node keeps the refusal (%s)", why);
            pb.node_present[1] = 1;
            joint(3, "WHEEL2", 0, -9, 2, 5);                 /* both on matrix 1's node */
            g_a.joints[3].part = SL_WA_PART_MATRIX + 1u;
            ck(sl_prop_derive(&g_a, &pb, &map, why) == 0,
               "a matrix named by a joint and ridden by none keeps the refusal (%s)", why);

            /* #96 THE PARTS ABOUT THEIR OWN PIVOTS: WHEEL1 named for matrix
             * 2 sits 8.06 item units off its node (-9, 2, -5) - past the
             * pivot tolerance (6.0) and on no node exactly - and nearer it
             * than any other node (matrix 1 17.5, the root 15.5) */
            joint(0, "BASE", -1, 0, 10, 0);
            joint(1, "WHEEL1", 0, -9, 6, -12);
            joint(2, "BEAN_END1", 1, -12, 6, -12);
            g_a.joint_count = 3;
            g_a.joints[0].part = SL_WA_PART_MATRIX + 1u;
            g_a.joints[1].part = SL_WA_PART_MATRIX + 3u;
            g_a.joints[2].part = SL_WA_PART_MATRIX + 3u;
            ck(sl_prop_derive(&g_a, &pb, &map, why) == 0,
               "#96 on a model no animation roots, a part off its node keeps the refusal (%s)", why);
            pb.animated = 1;                                 /* an aircraft's HEADER root */
            ck(sl_prop_derive(&g_a, &pb, &map, why) == 1 && map.count == 3
               && map.mtx[0] == 0 && map.mtx[1] == 2 && map.mtx[2] == 2
               && !map.own_pivot[0] && map.own_pivot[1] && map.own_pivot[2] && map.pivot_root == 0
               && fabs((double) map.bind[1][1] - 6.0) < 1e-4
               && fabs((double) map.carry[1][0] - 0.0) < 1e-3
               && fabs((double) map.carry[1][1] - 28.0) < 1e-3
               && fabs((double) map.carry[1][2] - -49.0) < 1e-3,
               "#96 a part nearest its own node but off it turns about its own joint (%s; carry "
               "%.3f %.3f %.3f)", why, (double) map.carry[1][0], (double) map.carry[1][1],
               (double) map.carry[1][2]);
            {
                /* root: a move of 100 along x; matrix 2: a quarter turn
                 * about z at its node, under the root */
                float gm[3][16], jm[3][16], v[3];
                int r, c2;
                memset(gm, 0, sizeof gm);
                for (r = 0; r < 3; r++)
                    for (c2 = 0; c2 < 4; c2++) gm[r][c2 * 5] = 1.0f;
                gm[0][12] = 100.0f;
                gm[2][0] = 0.0f; gm[2][1] = 1.0f; gm[2][4] = -1.0f; gm[2][5] = 0.0f;
                gm[2][12] = 37.0f; gm[2][13] = -56.0f; gm[2][14] = -35.0f;
                sl_chr_joint_matrices(&map, (const float (*)[16]) gm, 3, jm);
                /* the pivot goes where the root puts it */
                v[0] = -9.0f * jm[1][0] + 6.0f * jm[1][4] + -12.0f * jm[1][8] + jm[1][12];
                v[1] = -9.0f * jm[1][1] + 6.0f * jm[1][5] + -12.0f * jm[1][9] + jm[1][13];
                v[2] = -9.0f * jm[1][2] + 6.0f * jm[1][6] + -12.0f * jm[1][10] + jm[1][14];
                ck(fabs((double) v[0] - 37.0) < 1e-3 && fabs((double) v[1] - -28.0) < 1e-3
                   && fabs((double) v[2] - -84.0) < 1e-3,
                   "#96 the part's own pivot stays where the root places it (%.3f %.3f %.3f)",
                   (double) v[0], (double) v[1], (double) v[2]);
                /* a point one model unit along x of the pivot turns to +y */
                {
                    float px = -9.0f + 1.0f / 7.0f;
                    v[0] = px * jm[1][0] + 6.0f * jm[1][4] + -12.0f * jm[1][8] + jm[1][12];
                    v[1] = px * jm[1][1] + 6.0f * jm[1][5] + -12.0f * jm[1][9] + jm[1][13];
                    v[2] = px * jm[1][2] + 6.0f * jm[1][6] + -12.0f * jm[1][10] + jm[1][14];
                }
                ck(fabs((double) v[0] - 37.0) < 1e-3 && fabs((double) v[1] - -27.0) < 1e-3
                   && fabs((double) v[2] - -84.0) < 1e-3,
                   "#96 and turns about it by its matrix's turn relative to the root's "
                   "(%.3f %.3f %.3f)", (double) v[0], (double) v[1], (double) v[2]);
                map.own_pivot[1] = 0;
                sl_chr_joint_matrices(&map, (const float (*)[16]) gm, 3, jm);
                v[1] = -9.0f * jm[1][1] + 6.0f * jm[1][5] + -12.0f * jm[1][9] + jm[1][13];
                ck(fabs((double) v[1] - -28.0) > 1.0,
                   "#96 without the own pivot the part's joint goes to the model's node instead");
            }
            g_a.joints[1].bind[2] = -30;                     /* outside the item's box */
            ck(sl_prop_derive(&g_a, &pb, &map, why) == 0,
               "#96 a pivot outside the item's box keeps the refusal (%s)", why);
            joint(1, "WHEEL1", 0, -2, 9, -3);                /* nearer the root's node */
            g_a.joints[1].part = SL_WA_PART_MATRIX + 3u;
            ck(sl_prop_derive(&g_a, &pb, &map, why) == 0,
               "#96 a part nearer another node than its own keeps the refusal (%s)", why);
            pb.animated = 0;
            g_a = keep;
            memcpy(g_s, keep_s, sizeof keep_s);
        }

        /* THE WATCH HAND: joints on the model's matrices, one scale, one turn */
        {
            static struct sl_wa_asset keep;
            struct sl_vm_parts ap;
            keep = g_a;
            memset(&ap, 0, sizeof ap);
            ap.mtx_count = 9; ap.root_mtx = 5;
            /* the game's wrist (matrix 0) at (100, 20, 0), a watch hand
             * (matrix 1) 40 along -x, 10 along +y and 4 along +z from it */
            ap.node_present[0] = 1; ap.node_origin[0][0] = 100; ap.node_origin[0][1] = 20;
            ap.node_present[1] = 1; ap.node_origin[1][0] = 60;  ap.node_origin[1][1] = 30;
            ap.node_origin[1][2] = 4;
            memset(&g_a, 0, sizeof g_a);
            g_a.asset_class = SL_WA_CLASS_GUN;
            g_a.joint_count = 3;
            /* item: the same offset at half scale, a quarter turn back:
             * game (-40, 10, 4) = rot 1 of item (5, 20, 2) / 0.5 */
            joint(0, "SKEL_POSITION", -1, 0, 0, 0);
            joint(1, "SKEL_LF_WRIST", 0, 7, -3, 1);
            joint(2, "SKEL_HOUR", 1, 12, 17, 3);
            g_a.joints[1].part = SL_WA_PART_MATRIX + 1u;
            g_a.joints[2].part = SL_WA_PART_MATRIX + 2u;
            memset(g_v, 0, sizeof g_v); memset(g_s, 0, sizeof g_s);
            g_a.vertex_count = 2; g_a.vertices = g_v; g_a.skin = g_s;
            g_s[0].joint[0] = 1; g_s[0].weight[0] = 255;
            g_s[1].joint[0] = 2; g_s[1].weight[0] = 255;
            ck(sl_vm_derive(&g_a, &ap, &map, why) == 1 && fabs((double) map.scale - 0.5) < 1e-4
               && map.mtx[0] == -1 && map.mtx[1] == 0 && map.mtx[2] == 1
               && map.rot[1] == 1 && map.rot[2] == 1,
               "a watch hand rides the model's wrist and watch hand by matrix, one scale and one "
               "quarter turn (%s; scale %.3f rot %d)", why, (double) map.scale, map.rot[1]);
            {
                /* and the joint matrices carry an item point onto the game's */
                float game[9][16], jm[3][16], p[3] = { 12, 17, 3 }, e[3];
                int q;
                memset(game, 0, sizeof game);
                for (q = 0; q < 9; q++) {
                    game[q][0] = game[q][5] = game[q][10] = game[q][15] = 1.0f;
                    game[q][12] = ap.node_origin[q][0]; game[q][13] = ap.node_origin[q][1];
                    game[q][14] = ap.node_origin[q][2];
                }
                /* the node's geometry is drawn in its own frame: the wrist's
                 * matrix places the wrist, so the hand point, bound to the
                 * wrist, lands on the hand's node */
                sl_chr_joint_matrices(&map, (const float (*)[16]) game, 9, jm);
                for (q = 0; q < 3; q++)
                    e[q] = p[0] * jm[1][q] + p[1] * jm[1][4 + q] + p[2] * jm[1][8 + q] + jm[1][12 + q];
                ck(fabs((double) e[0] - 60.0) < 1e-3 && fabs((double) e[1] - 30.0) < 1e-3
                   && fabs((double) e[2] - 4.0) < 1e-3,
                   "an item point on the watch hand, bound to the wrist, lands on the game's hand "
                   "node (%.2f %.2f %.2f)", (double) e[0], (double) e[1], (double) e[2]);
            }
            g_a.joints[2].bind[1] = 30;            /* the hand off its node */
            ck(sl_vm_derive(&g_a, &ap, &map, why) == 0,
               "a watch hand whose joints no quarter turn places is refused (%s)", why);
            g_a.joints[2].bind[1] = 17;
            g_a.joints[2].part = 7u;               /* a Switches[] part beside matrices */
            ck(sl_vm_derive(&g_a, &ap, &map, why) == 0,
               "a joint naming a Switches[] part beside the matrices is refused (%s)", why);
            g_a.joints[2].part = SL_WA_PART_MATRIX + 4u;
            ck(sl_vm_derive(&g_a, &ap, &map, why) == 0,
               "a joint naming a matrix with no node is refused (%s)", why);
            g_a.joints[2].part = SL_WA_PART_MATRIX + 2u;
            g_s[1].joint[0] = 0;                   /* a vertex on the unnamed root */
            ck(sl_vm_derive(&g_a, &ap, &map, why) == 0,
               "a vertex bound to a joint naming no matrix is refused (%s)", why);
            g_a = keep;
        }

        /* THE ALTERNATIVES: one sleeve, the costume's; or all of them */
        {
            unsigned on[2] = { 0xffffffffu, 0xffffffffu };
            static struct sl_wa_asset keep;
            keep = g_a;
            /* the costume's cuff is Switches[5]; 4 and 6..9 are off */
            on[0] &= ~((1u << 4) | (1u << 6) | (1u << 7) | (1u << 8) | (1u << 9));
            ck(sl_vm_draw_visible(SL_WA_MESH_ALT | (6u << 8), on, SL_VM_ALT_ONE),
               "the sleeve serving the visible cuff is drawn");
            ck(!sl_vm_draw_visible(SL_WA_MESH_ALT | (5u << 8), on, SL_VM_ALT_ONE),
               "a sleeve serving another cuff is not");
            ck(!sl_vm_draw_visible(SL_WA_MESH_ALT, on, SL_VM_ALT_ONE),
               "a sleeve serving no cuff is never drawn as one");
            ck(sl_vm_draw_visible(SL_WA_MESH_ALT, on, SL_VM_ALT_ALL)
               && sl_vm_draw_visible(SL_WA_MESH_ALT | (5u << 8), on, SL_VM_ALT_ALL),
               "the source's composite draws every sleeve");
            ck(sl_vm_draw_visible(0u, on, SL_VM_ALT_ONE) && sl_vm_draw_visible(0u, on, SL_VM_ALT_ALL),
               "the arm, hand and watch draw in both");
            ck(!sl_vm_draw_visible(5u << 8, on, SL_VM_ALT_ALL),
               "a gated draw that is no sleeve keeps its switch in the composite");
            memset(&g_a, 0, sizeof g_a);
            g_a.asset_class = SL_WA_CLASS_GUN;
            g_a.mesh_count = 3;
            g_a.meshes[1].flags = SL_WA_MESH_ALT | (5u << 8);
            g_a.meshes[2].flags = SL_WA_MESH_ALT;
            ck(sl_vm_alt_serves(&g_a, 4) && !sl_vm_alt_serves(&g_a, 5) && !sl_vm_alt_serves(&g_a, -1),
               "a cuff is served only by a sleeve gated on it");
            g_a.meshes[1].flags = 5u << 8;
            ck(!sl_vm_alt_serves(&g_a, 4), "a gated draw that is no sleeve serves no costume");
            g_a = keep;
        }

        g_a.asset_class = SL_WA_CLASS_CHAR;
        ck(sl_vm_derive(&g_a, &pt, &map, why) == 0, "a character item is no view model");

        vis[0] = 0xffffffffu; vis[1] = 0xffffffffu;
        ck(sl_vm_mesh_visible(0u, vis), "an ungated draw is drawn");
        vis[0] &= ~(1u << 11);                  /* SW_HANDOFF4 off */
        ck(!sl_vm_mesh_visible((11u + 1u) << 8, vis), "a draw under an off switch is not drawn");
        ck(sl_vm_mesh_visible((13u + 1u) << 8, vis), "a draw under an on switch is drawn");
        ck(!sl_vm_mesh_visible(((13u + 1u) << 8) | ((11u + 1u) << 16), vis),
           "a draw under two switches needs both");
        vis[1] &= ~(1u << 3);                   /* switch 35 off */
        ck(!sl_vm_mesh_visible((35u + 1u) << 16, vis), "a switch past 31 is read from the second word");
        ck(!sl_vm_mesh_visible((64u + 1u) << 8, vis), "a switch past the table is never visible");
    }

    /* THE DESTROYED AUTOGUN: the share, and which vertices take it */
    {
        struct sl_wa_asset keep = g_a;
        struct sl_chr_map pm;
        ck(sl_prop_destroyed_keeps(SL_PROP_KIND_AUTOGUN) == 1
           && sl_prop_destroyed_keeps(SL_PROP_KIND_TANK) == 0
           && sl_prop_destroyed_keeps(SL_PROP_KIND_AIRCRAFT) == 0
           && sl_prop_destroyed_keeps(SL_PROP_KIND_GAS) == 0
           && sl_prop_destroyed_keeps(SL_PROP_KIND_PROP) == 0
           && sl_prop_destroyed_keeps(SL_PROP_KIND_VEHICLE) == 0,
           "only a destroyed autogun keeps its item; every other kind draws as the game's");
        ck(sl_prop_destroyed_shade(0, 1) == 255 && sl_prop_destroyed_shade(-1, 0) == 255,
           "an intact object's share is the whole colour");
        ck(sl_prop_destroyed_shade(1, 1) == 26 && sl_prop_destroyed_shade(2, 1) == 3
           && sl_prop_destroyed_shade(1, 0) == 204 && sl_prop_destroyed_shade(2, 0) == 163
           && sl_prop_destroyed_shade(3, 0) == 131,
           "the share is objDeform's: 0.1^L at or above the part's mid-height, 0.8^L below "
           "(%d %d %d %d)", sl_prop_destroyed_shade(1, 1), sl_prop_destroyed_shade(2, 1),
           sl_prop_destroyed_shade(1, 0), sl_prop_destroyed_shade(2, 0));
        ck(sl_prop_destroyed_shade(4, 0) < sl_prop_destroyed_shade(3, 0)
           && sl_prop_destroyed_shade(40, 0) >= 0 && sl_prop_destroyed_shade(40, 0) < 10
           && sl_prop_destroyed_shade(40, 1) == 0,
           "every further level darkens, and the share never leaves 0 .. 255");
        {
            struct sl_chr_map hm;
            memset(&hm, 0, sizeof hm);
            hm.ok = 1; hm.count = 1; hm.scale = 0.25f;
            hm.bind[0][0] = 10.0f; hm.bind[0][1] = 4.0f;
            ck(sl_prop_destroyed_height(&hm, 10.0f, 9.0f) == 5.0f
               && sl_prop_destroyed_height(&hm, 30.0f, 9.0f) == 5.0f,
               "a point's height is its y past the joint's bind, the scale left out");
            hm.rot[0] = 1;
            ck(sl_prop_destroyed_height(&hm, 17.0f, 100.0f) == 7.0f,
               "a quarter turn of the map takes the height from x (%g)",
               (double) sl_prop_destroyed_height(&hm, 17.0f, 100.0f));
            hm.rot[0] = 2;
            ck(sl_prop_destroyed_height(&hm, 0.0f, 9.0f) == -5.0f,
               "a half turn turns the height over");
            ck(sl_prop_destroyed_height(NULL, 1.0f, 2.0f) == 2.0f, "no map: the item's own y");
        }
        /* the fixture: vertex 0 on LEFTLOW (joint 5), vertex 1 on UP (joint 2) */
        fixture();
        memset(&pm, 0, sizeof pm);
        pm.ok = 1; pm.count = 6;
        pm.mtx[0] = 0; pm.mtx[1] = 0; pm.mtx[2] = 1; pm.mtx[3] = 2; pm.mtx[4] = 4; pm.mtx[5] = 3;
        ck(sl_prop_destroyed_vertex(&g_a, &pm, 0, 3) == 1 && sl_prop_destroyed_vertex(&g_a, &pm, 1, 3) == 0
           && sl_prop_destroyed_vertex(&g_a, &pm, 1, 1) == 1 && sl_prop_destroyed_vertex(&g_a, &pm, 0, 0) == 0,
           "a vertex takes the share when its joint rides the deformed part's matrix");
        g_s[1].joint[1] = 5; g_s[1].weight[0] = 100; g_s[1].weight[1] = 155;
        ck(sl_prop_destroyed_vertex(&g_a, &pm, 1, 3) == 1 && sl_prop_destroyed_vertex(&g_a, &pm, 1, 1) == 0,
           "the heaviest lane decides a blended vertex");
        ck(sl_prop_destroyed_vertex(&g_a, &pm, 0, -1) == 0 && sl_prop_destroyed_vertex(&g_a, &pm, 2, 3) == 0
           && sl_prop_destroyed_vertex(NULL, &pm, 0, 3) == 0 && sl_prop_destroyed_vertex(&g_a, NULL, 0, 3) == 0,
           "no matrix, no such vertex, no item or no map takes nothing");
        g_a.skin = NULL; pm.count = 1; pm.mtx[0] = 7;
        ck(sl_prop_destroyed_vertex(&g_a, &pm, 0, 7) == 1 && sl_prop_destroyed_vertex(&g_a, &pm, 1, 2) == 0,
           "a one-joint item takes the share whole when its joint rides that matrix");
        g_a = keep;
    }

    printf("%s: %d passed, %d failed\n", t_fail ? "CHARSKIN SELFTEST FAILED" : "charskin selftest",
           t_pass, t_fail);
    return t_fail ? 1 : 0;
}
#endif /* SL_CHR_SELFTEST */
