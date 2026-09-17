/**
 * Native-only byte-swap pass for model files (T4).
 *
 * Model files are big-endian on cartridge.  On N64 the CPU reads them
 * directly; natively every multi-byte field must be swapped exactly once,
 * at the same choke point where offsets are promoted to pointers
 * (sub_GAME_7F075A90 / modelPromoteNodeOffsetsToPointers walks each node
 * exactly once).  The hooks in model.c call into here under #ifndef __sgi;
 * this whole file compiles away on IDO, so the matching build never sees it.
 *
 * Deliberately not swapped yet, pending the code paths that read them:
 *   - render vertex arrays (DisplayListRecord.Vertices) and the Gfx display
 *     lists themselves — the DL/texture parse path gets its own pass
 *   - Op05/Op07 Data[400] blobs
 */
#ifndef __sgi

#include <ultra64.h>
#include "model.h"
#include "chrobjdata.h"
#include "objecthandler.h"

static u32 sl_w32(u32 v)
{
    return (v >> 24) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | (v << 24);
}

static u16 sl_w16(u16 v)
{
    return (u16) ((v >> 8) | (v << 8));
}

#define SW32(f) (*(u32 *) &(f) = sl_w32(*(u32 *) &(f)))
#define SW16(f) (*(u16 *) &(f) = sl_w16(*(u16 *) &(f)))

static void sl_coord3d_swap(coord3d *c)
{
    SW32(c->f[0]);
    SW32(c->f[1]);
    SW32(c->f[2]);
}

/* Whole-region word swap, for formats that are uniformly 32-bit words
 * (e.g. struct font: kerning table + fontchar records). */
void sl_swap_words(void *p, u32 nwords)
{
    u32 *w = p;
    u32 i;

    for (i = 0; i < nwords; i++)
        w[i] = sl_w32(w[i]);
}

/* Whole-region halfword swap, for formats that are uniformly 16-bit.
 * The briefing files (Ubrief*Z) are the first: 0x30 bytes with no 32-bit
 * field anywhere in them. */
void sl_swap_halfwords(void *p, u32 nhalves)
{
    u16 *h = p;
    u32 i;

    for (i = 0; i < nhalves; i++)
        h[i] = sl_w16(h[i]);
}

/* Model files bake the N64 ADDRESS of their compiled-in skeleton; the code
 * compares and dereferences header->Skeleton as a pointer, so natively the
 * field is relocated by value to the matching symbol.  The addresses are
 * the matching build's - fixed for good.  Unswapped skeleton pointers made
 * every file-model's root motion read garbage joint field indices: Bond's
 * body slid 13 units a frame off a near-static animation. */
extern struct ModelSkeleton SKELETON(standard_gun);
extern struct ModelSkeleton SKELETON(gun_unassigned);
extern struct ModelSkeleton SKELETON(gun_kf7);
extern struct ModelSkeleton SKELETON(player_gait_object);

static const struct { u32 n64; struct ModelSkeleton *native; } sl_skeleton_relocs[] = {
    { 0x8003a05c, &SKELETON(cctv) },
    { 0x8003a070, &SKELETON(console_one_screen) },
    { 0x8003a084, &SKELETON(console_four_screen) },
    { 0x8003a0b0, &SKELETON(tv_holder) },
    { 0x8003a0e0, &SKELETON(rotating_stuff) },
    { 0x8003a100, &SKELETON(eyelid_door) },
    { 0x8003a15c, &SKELETON(iris_door) },
    { 0x8003a170, &SKELETON(walletbond) },
    { 0x8003a19c, &SKELETON(car) },
    { 0x8003a1c8, &SKELETON(flying) },
    { 0x8003a1dc, &SKELETON(door) },
    { 0x8003a208, &SKELETON(tank) },
    { 0x8003a21c, &SKELETON(hat) },
    { 0x8003c4d8, &SKELETON(standard_object) },
    { 0x8003c4fc, &SKELETON(prop_weapon) },
    { 0x8003c570, &SKELETON(player_gait_object) },
    { 0x8003c6e4, &SKELETON(suit_lf_hand) },
    { 0x8003c714, &SKELETON(standard_gun) },
    { 0x8003c728, &SKELETON(gun_unassigned) },
    { 0x8003c76c, &SKELETON(gun_revolver) },
    { 0x8003c7ac, &SKELETON(gun_kf7) },
    { 0x8003d390, &SKELETON(g_weapon) },
    { 0x8003d400, &SKELETON(guard) },
};

static void sl_skeleton_reloc(ModelFileHeader *header)
{
    u32 v = (u32) header->Skeleton;
    u32 i;

    if (v == 0 || (v >> 24) != 0x80)
        return;                 /* absent, or already native */
    for (i = 0; i < sizeof sl_skeleton_relocs / sizeof sl_skeleton_relocs[0]; i++) {
        if (sl_skeleton_relocs[i].n64 == v) {
            header->Skeleton = sl_skeleton_relocs[i].native;
            return;
        }
    }
    {
        extern void sl_fatalf(const char *fmt, unsigned v);
        sl_fatalf("sightline native: unknown skeleton address %08x\n", v);
    }
}

/* Switches index array and textures table sit at the head of the file. */
void sl_model_head_swap(ModelFileHeader *header)
{
    s32 i;

    sl_skeleton_reloc(header);

    for (i = 0; i < header->numSwitches; i++)
        SW32(header->Switches[i]);

    for (i = 0; i < header->numtextures; i++)
        SW32(header->Textures[i].TextureID);
}

void sl_model_node_swap(ModelNode *node)
{
    SW16(node->Opcode);
    SW32(node->Data);
    SW32(node->Parent);
    SW32(node->Next);
    SW32(node->Prev);
    SW32(node->Child);
}

/* Collision vertex: coord16 xyz + index, then LinkedTo as a 32-bit offset
 * (the s/t interpretation applies to render vertices only). */
/* One s16 per vertex - see the citation at the call site. A NULL table is
 * normal: only the 18-display-list models merge points at all. */
void sl_model_pointusage_swap(s16 *usage, s32 nvertices)
{
    s32 i;

    if (usage == NULL || nvertices <= 0)
        return;
    for (i = 0; i < nvertices; i++)
        SW16(usage[i]);
}

void sl_model_collision_vertices_swap(Vertex *v, s32 n)
{
    s32 i;

    for (i = 0; i < n; i++) {
        SW16(v[i].coord.x);
        SW16(v[i].coord.y);
        SW16(v[i].coord.z);
        SW16(v[i].index);
        SW32(v[i].LinkedTo);
    }
}

void sl_model_children_swap(struct ModelRoData_Child *c, s32 n)
{
    s32 i;

    for (i = 0; i < n; i++) {
        SW16(c[i].unk02);
        SW32(c[i].unk04);
    }
}

void sl_model_rodata_swap(u32 type, union ModelRoData *data)
{
    s32 i;

    switch (type)
    {
        case MODELNODE_OPCODE_HEADER:
        case MODELNODE_OPCODE_OP20:
            SW16(data->Header.AnimPart);
            SW16(data->Header.MatrixIndex);
            SW32(data->Header.FirstGroup);
            SW16(data->Header.Group1);
            SW16(data->Header.Group2);
            SW16(data->Header.RwDataIndex);
            break;

        case MODELNODE_OPCODE_GROUP:
        case MODELNODE_OPCODE_OP03:
        case MODELNODE_OPCODE_OP17: /* promote walker reads this as Group too */
            sl_coord3d_swap(&data->Group.Origin);
            SW16(data->Group.JointID);
            SW16(data->Group.MatrixID0);
            SW16(data->Group.MatrixID1);
            SW16(data->Group.MatrixID2);
            SW32(data->Group.ChildGroup);
            SW32(data->Group.BoundingVolumeRadius);
            break;

        case MODELNODE_OPCODE_DL:
            SW32(data->DisplayList.Primary);
            SW32(data->DisplayList.Secondary);
            SW32(data->DisplayList.Vertices);
            SW16(data->DisplayList.numVertices);
            break;

        case MODELNODE_OPCODE_DLCOLLISION:
            SW32(data->DisplayListCollisions.Primary);
            SW32(data->DisplayListCollisions.Secondary);
            SW32(data->DisplayListCollisions.Vertices);
            SW16(data->DisplayListCollisions.numVertices);
            SW16(data->DisplayListCollisions.numCollisionVertices);
            SW32(data->DisplayListCollisions.CollisionVertices);
            SW32(data->DisplayListCollisions.PointUsage);
            SW16(data->DisplayListCollisions.ModelType);
            SW16(data->DisplayListCollisions.RwDataIndex);
            break;

        case MODELNODE_OPCODE_DLPRIMARY:
            SW32(data->DisplayListPrimary.numVertices);
            SW32(data->DisplayListPrimary.Vertices);
            SW32(data->DisplayListPrimary.Primary);
            break;

        case MODELNODE_OPCODE_OP05:
            SW32(data->Op05.NumChildren);
            SW32(data->Op05.Children);
            SW32(data->Op05.Vertices);
            SW32(data->Op05.Images);
            break;

        case MODELNODE_OPCODE_OP06:
            SW32(data->Op06.unk00);
            SW32(data->Op06.unk04);
            SW32(data->Op06.unk08);
            SW32(data->Op06.unk0C);
            SW32(data->Op06.unk10);
            break;

        case MODELNODE_OPCODE_OP07:
            SW32(data->Op07.unk00);
            SW32(data->Op07.unk04);
            SW32(data->Op07.NumChildren);
            SW32(data->Op07.Children);
            SW32(data->Op07.Vertices);
            SW32(data->Op07.Images);
            SW16(data->Op07.unk1A8);
            SW16(data->Op07.RwDataIndex);
            break;

        case MODELNODE_OPCODE_LOD:
            SW32(data->LOD.MinDistance);
            SW32(data->LOD.MaxDistance);
            SW32(data->LOD.Affects);
            SW16(data->LOD.RwDataIndex);
            break;

        case MODELNODE_OPCODE_BSP:
            sl_coord3d_swap(&data->BSP.Point);
            sl_coord3d_swap(&data->BSP.Vector);
            SW32(data->BSP.leftChild);
            SW32(data->BSP.rightChild);
            SW16(data->BSP.reserved);
            SW16(data->BSP.RwDataIndex);
            break;

        case MODELNODE_OPCODE_BBOX:
            SW32(data->BoundingBox.ModelNumber);
            for (i = 0; i < 6; i++)
                SW32(data->BoundingBox.Bounds.AsArray[i]);
            break;

        case MODELNODE_OPCODE_OP11:
            for (i = 0; i < 16; i++)
                SW32(data->Op11.unk0c[i]);
            SW32(data->Op11.BoundingVolumeRadius);
            SW16(data->Op11.RwDataIndex);
            SW16(data->Op11.unk46);
            break;

        case MODELNODE_OPCODE_GUNFIRE:
            sl_coord3d_swap(&data->Gunfire.Offset);
            sl_coord3d_swap(&data->Gunfire.Size);
            SW32(data->Gunfire.Image);
            SW32(data->Gunfire.Scale);
            SW16(data->Gunfire.RwDataIndex);
            break;

        case MODELNODE_OPCODE_SHADOW:
            SW32(data->Shadow.pos.f[0]);
            SW32(data->Shadow.pos.f[1]);
            SW32(data->Shadow.size.f[0]);
            SW32(data->Shadow.size.f[1]);
            SW32(data->Shadow.image);
            SW32(data->Shadow.Header);
            SW32(data->Shadow.Scale);
            break;

        case MODELNODE_OPCODE_OP14:
            sl_coord3d_swap(&data->Op14.pos);
            SW32(data->Op14.Scale);
            break;

        case MODELNODE_OPCODE_INTERLINK:
            sl_coord3d_swap(&data->Interlinkage.pos);
            sl_coord3d_swap(&data->Interlinkage.pos2);
            SW32(data->Interlinkage.Scale);
            break;

        case MODELNODE_OPCODE_OP16:
            sl_coord3d_swap(&data->Op16.pos);
            SW16(data->Op16.nodeindex0c);
            SW16(data->Op16.nodeindex0e);
            SW16(data->Op16.nodeindex10);
            SW16(data->Op16.unk12);
            SW32(data->Op16.Scale);
            break;

        case MODELNODE_OPCODE_SWITCH:
            SW32(data->Switch.Controls);
            SW16(data->Switch.RwDataIndex);
            break;

        case MODELNODE_OPCODE_HEAD:
            SW16(data->HeadPlaceholder.RwDataIndex);
            break;

        case MODELNODE_OPCODE_GROUPSIMPLE:
            /* B-028: Origin was missed here, and it is the ONLY thing that
             * positions a held weapon or a hat.  model.c:1782/1787 feed it
             * straight to matrix_4x4_set_identity_and_position, so an
             * unswapped coord3d becomes the translation row of the model's
             * render_pos.  Measured on streets before the fix: a guard's KF7
             * kept a correct 3x3 (identical to its basemtx) with row 3 at
             * 1.7e28, -8.0e27, -5.0e27 against the body's -74.9, -9.4, -266.6
             * - transformed clean out of the world, while every counter along
             * the path read healthy (chrRenderHeldWeapon emitted 1167 of 1167,
             * modelRenderNodeGundl branched to the weapon's DL 1166 times).
             * `grep -rn GroupSimple src/` finds these three SW lines as the
             * only native handling of this record anywhere in the tree; Origin
             * was swapped nowhere.
             * Why only held items and hats: "object templates.txt" (GE
             * Documentation/Display Lists and Object Generation/Objects) lists
             * the skeleton templates, and Pchr*Z (item pickups: "0015 pos:
             * primary model", "0015 pos: gunfire effect") and Phat*Z (hats:
             * "0015 pos: primary model") are the only classes whose parts are
             * 0015 commands.  Bodies, doors, tanks and vehicles position with
             * 0002, whose Origin the GROUP case above has always swapped. */
            sl_coord3d_swap(&data->GroupSimple.Origin);
            SW16(data->GroupSimple.Group1);
            SW16(data->GroupSimple.Group2);
            SW32(data->GroupSimple.BoundingVolumeRadius);
            break;

        default:
            break;
    }
}

/*
 * Display lists: value-preserving word swap so gbi word reads and native DL
 * construction agree; byte-positional opcode parsers use SL_DLOP instead.
 * Walk each list to its G_ENDDL, following G_DL calls/branches within the
 * file (segment-offset addressing relative to `base`).
 */
#define SL_G_DL     0x06
#define SL_G_ENDDL  0xB8

static void *sl_dl_seen[1024];
static s32 sl_dl_seen_count;

static void sl_gfx_dl_swap(u8 *dl, u8 *base)
{
    s32 i;
    u32 w0, w1;
    u8 op;

    for (i = 0; i < sl_dl_seen_count; i++)
        if (sl_dl_seen[i] == dl)
            return;
    if (sl_dl_seen_count >= 1024)
        *(volatile int *) 0 = 0;    /* visited-set overflow: fail loudly */
    sl_dl_seen[sl_dl_seen_count++] = dl;

    for (;;) {
        op = dl[0];                 /* still wire order at this point */
        w0 = ((u32) dl[0] << 24) | ((u32) dl[1] << 16) | ((u32) dl[2] << 8) | dl[3];
        w1 = ((u32) dl[4] << 24) | ((u32) dl[5] << 16) | ((u32) dl[6] << 8) | dl[7];
        *(u32 *) dl       = w0;
        *(u32 *) (dl + 4) = w1;

        if (op == SL_G_ENDDL)
            return;

        if (op == SL_G_DL) {
            sl_gfx_dl_swap(base + (w1 & 0xffffff), base);
            if (((w0 >> 16) & 0xff) == 1)   /* G_DL_NOPUSH: branch, list ends */
                return;
        }

        dl += 8;
    }
}

/* Render vertices: 6 leading u16s each (coord16 xyz, index, s, t);
 * colors are bytes.  Promoted pointers, once per array. */
static void *sl_vtx_seen[256];
static s32 sl_vtx_seen_n;

static void sl_model_vertices_swap(Vertex *v, s32 n)
{
    s32 i;

    if (v == NULL || n <= 0)
        return;
    for (i = 0; i < sl_vtx_seen_n; i++)
        if (sl_vtx_seen[i] == v)
            return;
    if (sl_vtx_seen_n < 256)
        sl_vtx_seen[sl_vtx_seen_n++] = v;
    for (i = 0; i < n; i++) {
        SW16(v[i].coord.x);
        SW16(v[i].coord.y);
        SW16(v[i].coord.z);
        SW16(v[i].index);
        SW16(v[i].s);
        SW16(v[i].t);
    }
}

/* Swap every display list a model file references, and every render vertex
 * array its DL records point at.  Called after the promote pass (nodes are
 * native and walkable); DL values remain segment offsets, resolved against
 * the file base as the parsers do, while Vertices pointers were promoted. */
void sl_model_dls_swap(ModelFileHeader *header)
{
    ModelNode *node = NULL;
    Gfx *gdl = NULL;
    u8 *base = (u8 *) header->Switches;

    sl_dl_seen_count = 0;

    for (;;) {
        modelIterateDisplayLists(header, &node, &gdl);
        if (gdl == NULL)
            break;
        sl_gfx_dl_swap(base + ((u32) gdl & 0xffffff), base);
    }

    sl_vtx_seen_n = 0;
    node = header->RootNode;
    while (node != NULL) {
        u32 type = node->Opcode & 0xff;
        union ModelRoData *ro = node->Data;

        if (ro != NULL) {
            if (type == MODELNODE_OPCODE_DL)
                sl_model_vertices_swap(ro->DisplayList.Vertices,
                                       ro->DisplayList.numVertices);
            else if (type == MODELNODE_OPCODE_DLCOLLISION) {
                sl_model_vertices_swap(ro->DisplayListCollisions.Vertices,
                                       ro->DisplayListCollisions.numVertices);
                /* B-042.  The POINTER to PointUsage is swapped with the rest
                 * of the record, but its CONTENTS never were - this file's own
                 * header listed it as "count not yet confirmed", and that gap
                 * hung the game.
                 *
                 * chrCreateBloodStain (chr.c:3305) walks a chain through this
                 * table - `index = PointUsage[index]` until index < 0 - so
                 * unswapped s16 indices send it somewhere wrong and it can
                 * cycle forever. Caught as a HANG by the frame watchdog with
                 * that exact stack, shooting a guard.
                 *
                 * It hid the way B-037 did: the terminator is -1, and 0xFFFF
                 * is byte-order invariant, so the chain still LOOKS correctly
                 * terminated while every real index in it is wrong.
                 *
                 * The count is now confirmed, from
                 * "Display Lists and Object Generation/Objects/Point
                 * Merging.txt": "Usage tables have one entry for each point in
                 * the display list's point table." Its worked example is a
                 * piece with "2E normal points" whose usage table holds
                 * exactly 0x2E entries - so the length is numVertices, one
                 * s16 per vertex, and this stays inside the table. */
                sl_model_pointusage_swap(ro->DisplayListCollisions.PointUsage,
                                         ro->DisplayListCollisions.numVertices);
            }
            else if (type == MODELNODE_OPCODE_DLPRIMARY)
                /* THE COUNT IS FLARES, NOT POINTS. "Display Lists and Object
                 * Generation/Objects/1st person/G-Z Models.txt" (16 command:
                 * gunfire): "0x0 number of flares created (4 points each, 1
                 * 04+B1-mapping command for each)", and the reader agrees -
                 * dorottex (model.c:4427) allocates numVertices * 4 and walks
                 * src += 4 per flare. Swapping numVertices records left every
                 * point past the first six of the AR33's star in ROM order,
                 * read as coordinates in the tens of thousands, so the flash
                 * quads covered the whole frame at 50% alpha (the "blinding"
                 * muzzle flash, measured 2026-09-15: petal 0 sane at ~200
                 * model units, petals 1-5 at 6000-31000; the cartridge's star
                 * is ~25 world units). */
                sl_model_vertices_swap(ro->DisplayListPrimary.Vertices,
                                       ro->DisplayListPrimary.numVertices * 4);
        }

        if (node->Child != NULL) {
            node = node->Child;
        } else {
            while (node != NULL) {
                if (node->Next != NULL) {
                    node = node->Next;
                    break;
                }
                node = node->Parent;
            }
        }
    }
}

#endif /* !__sgi */
