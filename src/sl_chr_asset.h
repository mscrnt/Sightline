/*
 * Sightline - the CHARACTER-ASSET seam (#70): a character's own model drawn
 * from a presentation's replacement item, deformed by the game's own skeleton.
 *
 * THE CONTRACT, in one sentence: when the selected presentation carries a
 * replacement for the body (or the head) a character wears, the game's own
 * geometry for that part is not emitted and the replacement is drawn in its
 * place, at the same point of the display list, in the pose the game
 * animated this frame. Nothing about the character's simulation moves: its
 * skeleton, animation, collision, hit boxes, AI and model ids are the game's,
 * and the per-node relations the render walk applies (distance LOD, switches,
 * the head attachment) still run - only the display-list emission of the
 * replaced part is withheld.
 *
 * IDENTITY. A character model is one of the game's own files, C<item>Z, and
 * the replacement is the world-asset provider's (class, item) with class CHAR
 * for a body and HEAD for a head: Cgreatguard2Z is ("char", "greatguard2"),
 * CheadleeZ is ("head", "headlee"). No table pairs them; the name is the key
 * in both vocabularies. A part with no replacement draws exactly as before.
 *
 * THE COMPOSITION. The game's body model carries the neck its separate head
 * sits on; a replacement body carries its neck inside its built-in head
 * block, and a replacement head item carries a neck of its own. So a
 * character that wears a separate head is drawn with the replacement body
 * only when the head is replaced too - under the game's own head nothing
 * would draw a neck - while a replacement head on the game's body composes
 * (sl_chr_compose, and THE NECK in src/native/sl_chr_asset.c).
 *
 * THE SKELETON RELATION. A replacement item is authored in its own bind
 * space: the game's joints, by the source's own names, at bind positions a
 * uniform scale larger, with the limbs hanging where the game's part frames
 * lay them along an axis. sl_chr_derive relates the two from the data alone -
 * the item's joint table and the game model's GROUP tree - by matching each
 * joint's child bones, direction for direction, under a quarter turn about
 * the game's z axis, and taking the scale from the matched bone lengths. A
 * model the two do not match on is refused and draws as the game's.
 *
 * THE BRIDGE. The seam emits one display-list command where the character's
 * geometry begins, in the family sl_asset_override.h documents (ucode05
 * opcode 0x02, which no GoldenEye list carries):
 *
 *   w0 = 0x02 'S' 'C' 'H'        SL_CHR_DL_W0
 *   w1 = the struct sl_chr_draw  a record of this frame's draw, owned by the
 *                                seam's ring (sl_chr_draw_ok validates it)
 *
 * PLAIN TYPES ONLY, no includes: the game (behind #ifndef __sgi), the native
 * seam and the renderer all read this header.
 */
#ifndef SL_CHR_ASSET_H
#define SL_CHR_ASSET_H

#define SL_CHR_DL_W0          0x02534348u
#define SL_CHR_MAX_JOINTS     32
#define SL_CHR_PARTS          2          /* body, head */
#define SL_CHR_PART_BODY      0
#define SL_CHR_PART_HEAD      1
#define SL_CHR_DRAW_MAGIC     0x43485244u   /* "CHRD" */
/* The world-asset provider's classes for the two parts - the same numbers as
 * SL_WA_CLASS_CHAR / SL_WA_CLASS_HEAD in src/gfx/sl_gfx_worldasset.h, which
 * the renderer side checks at compile time (sl_gfx_charskin.c). */
#define SL_CHR_CLASS_CHAR     3u
#define SL_CHR_CLASS_HEAD     4u

/* The game model's skeleton as the render walk sees it: every GROUP node of
 * the body, in tree order, with the index of its nearest GROUP ancestor
 * (-1 at the top), its origin in that ancestor's frame, and the matrix its
 * geometry is drawn under. */
struct sl_chr_tree {
    int   count;
    int   parent[SL_CHR_MAX_JOINTS];
    float origin[SL_CHR_MAX_JOINTS][3];
    int   mtx[SL_CHR_MAX_JOINTS];
};

/* How one replacement item rides that skeleton, per item joint: the game
 * matrix it takes (-1: none - no vertex may be bound to it), the quarter
 * turns about z from the item's bind orientation into that game frame, and
 * its bind position; scale is item units per game unit. */
struct sl_chr_map {
    int   ok;
    int   count;
    int   mtx[SL_CHR_MAX_JOINTS];
    int   rot[SL_CHR_MAX_JOINTS];
    float bind[SL_CHR_MAX_JOINTS][3];
    float scale;
    /* THE PART ABOUT ITS OWN PIVOT (sl_prop_derive, #96): where own_pivot[j]
     * is set, joint j turns about its own bind by its matrix's turn relative
     * to the root's (pivot_root), carried by carry[j] in the root's frame;
     * a map that sets none is drawn exactly as before. */
    int   own_pivot[SL_CHR_MAX_JOINTS];
    float carry[SL_CHR_MAX_JOINTS][3];
    int   pivot_root;
};

/* THE DYNAMIC DOOR's cut (below, sl_door_cut_of): keep the MODEL-space
 * points where sign * (coordinate[axis] - value) >= 0. */
struct sl_door_cut {
    int   on;
    int   axis;
    int   sign;
    float value;
};

/* One character's draw this frame. */
struct sl_chr_draw {
    unsigned magic;                          /* SL_CHR_DRAW_MAGIC */
    const void *asset[SL_CHR_PARTS];         /* the provider's items, or NULL */
    const struct sl_chr_map *map[SL_CHR_PARTS];
    const void *mtx;                         /* the model's matrices (N64 Mtx) */
    int   mtx_count;
    unsigned char shade[4];                  /* the character's shade register */
    int   wears_head;                        /* a separate head is worn: the
                                                body's built-in head is not
                                                drawn */
    unsigned char fade;                      /* 255; below it, the game's own
                                                fade of the player's body */
    struct sl_door_cut cut;                  /* THE DYNAMIC DOOR: the body is
                                                cut at this half-space (on) */
    float seat[6];                           /* THE WORN HAT: the per-head
                                                offset (x, y, z) and size
                                                (x, y, z) the game placed this
                                                hat with, to be taken back
                                                (sl_hat_unseat); all zero:
                                                none */
    float rest;                              /* and how far it rises on the
                                                head item (sl_hat_rest) */
    unsigned char passes;                    /* THE PROP'S TWO PASSES: which of
                                                the item's draws this record
                                                draws (SL_CHR_PASS_*); 0 =
                                                every draw at once */
    unsigned char destroyed;                 /* THE DESTROYED AUTOGUN: the game's
                                                destroyed level of the object
                                                (objGetDestroyedLevel), 0 intact */
    int   destroyed_mtx;                     /* and the matrix the model's first
                                                display list - the part the game
                                                deforms - is drawn under; -1 none */
};

/* THE PROP'S TWO PASSES. The game draws its objects twice, the solid lists
 * in one walk and the translucent ones in a later walk after every solid
 * surface of the frame (chrobjRenderProp's flags: 1 solid, 2 translucent,
 * 3 both - a fading object, in the later walk only). A prop item's solid
 * draws ride the first walk and its translucent draws the second, so what
 * stands behind the glass is drawn before the glass blends over it. */
#define SL_CHR_PASS_SOLID        1u
#define SL_CHR_PASS_TRANSLUCENT  2u

/* THE SCREEN'S PLACE. A monitor's screen stays the game's (sl_prop_takes):
 * a four-vertex quad the game textures with its animated screen image. An
 * item may lay its own screen rectangle on the same face - a quad whose
 * texture coordinates span the whole image, the surface a screen image is
 * laid on - where the game's quad lies a little off it (prop/console_sev2b:
 * the item's screen is wider and shorter than the game's, which then crosses
 * the bezel). `quad` is the game's screen, its four vertices as the node's
 * matrix takes them; `out` the same four moved, in the quad's own plane,
 * onto the item's screen rectangle as that plane sees it, then each set on
 * whole units in front of the item's face (the side away from the item's
 * box centre) where that plane would leave it behind. 1 when the item
 * has such a quad on the screen's face (every corner off the screen's plane
 * by at most 5% of its diagonal, overlapping it by at least half) and it
 * differs from the game's; 0 otherwise, `out` = `quad`. Renderer side,
 * pure. */
int sl_screen_place(const void *asset, const struct sl_chr_map *map,
                    const float quad[4][3], float out[4][3]);

/* The renderer's half: this frame's vertex buffer of one screen (the four
 * vertices the game built, read-only) is drawn at `xyz`, in the game's own
 * vertex units. Render-only; the game's buffer is untouched. */
void sl_gfx_screen_place(const void *vtx, const short xyz[4][3]);

/* Which of a class-PROP item's draws it has: SL_CHR_PASS_SOLID when any
 * draw is not translucent, SL_CHR_PASS_TRANSLUCENT when any is. Renderer
 * side, pure. */
unsigned sl_prop_item_passes(const void *asset);

/* Does a record with `passes` (0: every draw) draw a mesh that is
 * `translucent`? Pure. */
int sl_chr_pass_draws(unsigned passes, int translucent);

/* Which parts of one character draw from their items: SL_CHR_DRAW_BODY and
 * SL_CHR_DRAW_HEAD (the game's own lists of that part are withheld), and
 * SL_CHR_DRAW_BUILTIN_HEAD when the body draws its own built-in head block.
 * `wears_head`: the character wears a separate head model; `body` / `head`:
 * a replacement for that part exists and rides the model. 0 = the game's
 * character, unchanged. Whenever the body draws, something drawn owns the
 * neck: its built-in head block, or a replacement head. Pure. */
#define SL_CHR_DRAW_BODY          1u
#define SL_CHR_DRAW_HEAD          2u
#define SL_CHR_DRAW_BUILTIN_HEAD  4u
unsigned sl_chr_compose(int wears_head, int body, int head);

/* A body item that carries its OWN head (src/gfx/sl_gfx_worldasset.h
 * SL_WA_MESH_OWN_HEAD): the remaster draws that body with the head it
 * carries, always, and gives it no place to wear another. Such an item stands
 * for the game's body AND the head the game's model wears - the game's head
 * lists are withheld with the body's, and no head item is consulted - so the
 * neck is the item's own and nothing is composed across the two
 * vocabularies. Renderer side, pure. */
int sl_chr_item_owns_head(const void *asset);

/* THE PLAYER'S EXTERNAL BODY. The game builds it from private copies of its
 * body and head files (bondview2.c solo_char_load) and records neither number
 * on the player's character in this build, so the seam cannot know it from
 * the character the way it knows a guard. The game's own choice is handed
 * over where it is made, read-only; the body then draws through the same seam
 * as any character's (SL_BOND_ASSETS=0 keeps it the game's). */
void sl_chr_player_model(const void *model, const void *hdr, int body, int head);

/* Renderer side (src/gfx/sl_gfx_charskin.c). Relate a replacement item to
 * the game skeleton; 1 and *out filled with ok=1, or 0 with a reason written
 * to `why` (at least 96 bytes). Pure: no GL, no files, no globals. */
int sl_chr_derive(const void *asset, const struct sl_chr_tree *tree,
                  struct sl_chr_map *out, char *why);

/* Native side (src/native/sl_chr_asset.c). Is this pointer a record the
 * seam handed out, and still current? */
int sl_chr_draw_ok(const void *p);

/* =========================================================================
 * THE STAGE'S RIGID PROPS (#71) - a prop model (P<item>Z: a mainframe, a
 * desk, a monitor) drawn from a presentation's replacement item, "prop/<item>"
 * (class PROP, SL_WA_CLASS_PROP).
 *
 * THE CONTRACT. The game draws a standard prop or a monitor from its
 * model (propobj.c, the object render) under the prop's own matrix; the
 * prop's placement, collision, interaction, damage and destruction are the
 * game's and stay so. The item is one rigid piece; nothing in it says where
 * the game's model sits, so the bundle carries the REFERENCE box - the box of
 * the source's own generation of the game's model, in the item's space - and
 * the seam measures the box of the game model's own vertices once per model
 * file; the two boxes relate the item to the model's root frame by one
 * scale and one offset (sl_prop_derive). When the item rides the model, the
 * model's lists are withheld (a monitor's animated screens excepted, which the
 * game goes on drawing over the item) and the item is drawn by the character
 * bridge, with the prop's shade, in their place. A prop that is damaged, a
 * model with more than one matrix, or one whose box the item's does not
 * match, draws as the game's.
 *
 * THE MOVING PROP. A vehicle's model moves parts of itself under matrices of
 * their own (its wheels: propobj.c, the vehicle branch of the object render,
 * turns and steers them every frame), and the remaster's item for it is a
 * SKINNED item with a joint per part, named as the game names that part's
 * matrix. The bundle carries those joints with the matrix each rides
 * (SL_WA_PART_MATRIX + 1 + m) and the reference box as any prop's. The box
 * relates the item to the model's root frame as above, and each joint that
 * names a matrix is set where that relation puts the model's own node for
 * the matrix - so the part turns about the game's pivot, under the matrix the
 * game computed; a joint that names none rides the model's root. The model's
 * numMatrices no longer refuses such an item; it still refuses a one-piece
 * item on a model that moves parts. Where a joint's name puts it on a node
 * it does not sit on, the item is related by PLACE instead - every joint
 * that rides a matrix on the node it sits on exactly - or refused (#94,
 * sl_gfx_charskin.c prop_parts_by_place: the drone gun's twin barrels).
 * ========================================================================= */
#define SL_CHR_CLASS_PROP     6u            /* SL_WA_CLASS_PROP */

/* The game prop model's own geometry: the box of every display-list vertex
 * in MODEL space (each placed by its positioning GROUP ancestors' origins),
 * the root GROUP's origin in that space and matrix, and every GROUP node by
 * the matrix it is drawn under, with its origin in MODEL space (THE MOVING
 * PROP). */
#define SL_PROP_MAX_MTX       64
struct sl_prop_box {
    float lo[3], hi[3];
    float root_origin[3];
    int   root_mtx;
    int   mtx_count;
    unsigned char node_present[SL_PROP_MAX_MTX];
    float node_origin[SL_PROP_MAX_MTX][3];
    int   turn_ok;                 /* THE TURNED ITEM may be asked for: the     */
                                   /* object seam's models; 0 elsewhere         */
    int   fit_ok;                  /* THE FITTED ITEM (D-038) may be asked for:  */
                                   /* items the presentation names; 0 elsewhere */
    int   animated;                /* rooted on a HEADER node, the part its     */
};                                 /* animation positions (the aircraft): #96  */

/* THE FITTED ITEM (D-038). The source's item for a game model is normally the
 * game's model remastered, and its REFERENCE box (the source's original
 * generation) is the game model's in the item's space - which is what the
 * box test checks. An item the remaster authored as ANOTHER object has no
 * such box: prop/chrplastique, the C4 the game places on Frigate's bridge
 * and engine-room consoles, is in both generations a round mounted device
 * (-421 .. 536 x -205 .. 235 x -208 .. 208 item units) where the game's
 * model is a brick (-120 .. 120 x 0 .. 104 x -60 .. 60). For an item the
 * presentation names (sl_presentation_prop_fit) a box that fails the test is
 * FITTED instead: one scale, the smallest that keeps the item inside the
 * model's box on every axis (its proportions kept), its box centred on the
 * model's in x and z and its bottom on the model's bottom, so it stands where
 * the game's model stands, on the same surface. The game's matrix, placement,
 * state and destruction drive it as any prop's. */

/* Renderer side (src/gfx/sl_gfx_charskin.c): relate a class-PROP item to the
 * game model by the two boxes - the scale their extents agree on (every axis
 * within SL_CHR_SCALE_TOL) and the offset their centres give - as a one-joint
 * map on the root matrix. 1 and *out filled, or 0 with the reason (at least
 * 96 bytes). Pure.
 *
 * THE TURNED ITEM (#80). A one-piece item whose box is not the model's as it
 * stands may be the model's under a quarter turn about z: the remaster's
 * carried items (a weapon, a key) are authored in the carrying hand's frame,
 * the item's y along the game model's x (prop/chrkalash: 180 x 3066 x 1320
 * item units against the game's 653 x 39 x 281, 4.69 times; its pose is a
 * whole guard's skeleton with SKEL_GUN on the right wrist). Two turns fit any
 * box that one does, a half turn apart. Both the item and the game's model
 * are laid out from their own ORIGIN at the carrier's hand - the game's model
 * runs from its origin to one side along its long axis (the AK 4.9 .. -648.1,
 * the Klobb's centre -182 of 364), the item from the wrist along its y - so the
 * turn taken is the one that puts the item's box on the same side of the
 * origin along the model's long axis. A model centred on its origin (its box
 * centre within SL_PROP_GRIP_SIDE of its long extent) does not choose, and the
 * item is refused. A box the model matches unturned is taken unturned, as
 * before; only a box with `turn_ok` (the object seam's) may be turned. */
#define SL_PROP_GRIP_SIDE 0.1f
int sl_prop_derive(const void *asset, const struct sl_prop_box *game,
                   struct sl_chr_map *out, char *why);

/* Does a class-PROP item carry joints that name the model's matrices (THE
 * MOVING PROP)? Renderer side, pure. */
int sl_prop_item_moves(const void *asset);

/* THE FIXED PARTS (#92). A model of more than one matrix moves parts - the
 * seam's reason to refuse a one-piece item on it - only when one of its
 * matrices is set by a node that can turn or move it: a GROUP or OP03 node
 * (an animation or the object's own code turns it: a vehicle's wheels) or a
 * HEADER (an animated model's part). A matrix set by a GROUPSIMPLE node is
 * its parent's matrix times the node's own origin and nothing else
 * (model.c, process_15_subposition), so a model whose every matrix is a
 * GROUPSIMPLE's is one rigid piece at fixed offsets, and a one-piece item
 * rides its root matrix exactly. The weapon skeleton (skeleton_prop_weapon)
 * names two GROUPSIMPLE joints, and four weapon models carry both matrices
 * (PchrrocketlaunchZ, Pchrm16Z, Pchrfnp90Z, PchrlaserZ); MEASURED on the
 * rocket launcher lying in Depot's warehouse, which the seam refused on its
 * two matrices while the game's white tube drew under XBLA (playtest run
 * 20261009-122825-lvl30, mark 2): its walk finds no other matrix node. Its
 * remaster item then meets the box test, which refuses it (B-DEP1).
 * `num_matrices` is the model's count, `moving_nodes` the GROUP / OP03 /
 * HEADER nodes in its tree, `simple_matrices` the distinct matrices its
 * GROUPSIMPLE nodes set.
 * 1 when the model is more than one matrix, all of them fixed. Pure. */
int sl_prop_parts_fixed(int num_matrices, int moving_nodes, int simple_matrices);

/* WHICH OBJECTS A PROP ITEM MAY STAND IN FOR, and how many of the model's
 * first switches keep drawing over it. `kind` is the object's setup type
 * (SL_PROP_KIND_*; any other kind - a door, glass, a weapon or key lying
 * in the level - is not taken),
 * `num_switches` the model's switch count. Returns -1 when the object is not
 * taken, else the count of its SCREENS: the nodes the game animates a
 * monitor's text on (propobj.c, process_monitor_animation_microcode) -
 * Switches[0] on a single monitor, Switches[0..3] on a multi-screen one. A
 * door is never taken HERE: the doors an item may stand in for are the
 * dynamic doors below, which have a contract of their own. A GAS PROP (the
 * bottling tanks and gas barrels a shot sets venting) is a one-piece model
 * drawn by the same object render; its item rides the matrix as a standard
 * prop's does, and the damage that vents it stays the game's (a damaged
 * object draws as the game's). GLASS (breakable glass: the lab glassware)
 * is taken the same way, its item drawn blended by its own coverage; a
 * shattered one draws as the game's. Windows are glass too on some stages,
 * and stay the game's because the preparation refuses a pane: no item
 * exists for one. TINTED glass is not taken. BODY ARMOUR lying in the
 * level is a one-piece model the object render draws too; its item rides
 * it until the player takes it (the pickup is the game's). An AMMO BOX is
 * the same: a crate drawn whole until it is collected. A worn HAT is drawn inside its character's
 * walk (THE WORN HAT, src/native/sl_chr_asset.c); the kind here is the hat
 * the game drops when it is shot off. The AUTOGUN, the AIRCRAFT and the TANK
 * are moving props, taken as the vehicle is: the object render computes
 * their parts' matrices from the game's own state (the gun's yaw and pitch,
 * the rotor's turn and the aircraft's animation, the turret, gun and hatch)
 * and the item's joints ride them by the skeleton's names; a destroyed
 * aircraft or tank draws as the game's, a destroyed autogun keeps its item
 * (THE DESTROYED AUTOGUN, below). The KEY, the ALARM and the COLLECTABLE
 * are one-piece props (THE COLLECTABLE, below). Renderer side, pure. */
#define SL_PROP_KIND_DOOR           1
#define SL_PROP_KIND_PROP           3
#define SL_PROP_KIND_KEY            4
#define SL_PROP_KIND_ALARM          5
#define SL_PROP_KIND_COLLECTABLE    8
#define SL_PROP_KIND_MONITOR       10
#define SL_PROP_KIND_MULTI_MONITOR 11
#define SL_PROP_KIND_AUTOGUN       13
#define SL_PROP_KIND_HAT           17
#define SL_PROP_KIND_AMMO          20
#define SL_PROP_KIND_ARMOUR        21
#define SL_PROP_KIND_GAS           36
#define SL_PROP_KIND_VEHICLE       39
#define SL_PROP_KIND_AIRCRAFT      40
#define SL_PROP_KIND_GLASS         42
#define SL_PROP_KIND_TANK          45
#define SL_PROP_MAX_SCREENS         4

/* THE COLLECTABLE (#80). A key, an alarm and a collectable - a weapon, lying
 * where the game put it or carried by a guard - are drawn by the object
 * render from a P<item>Z model like a standard prop's, and their remaster
 * items (prop/chrkalash, prop/chrkeybolt, prop/alarm2) are one rigid piece
 * each: taken as any one-piece prop, related by the two boxes. The game keeps
 * the pickup, the key's use, the alarm's state, the weapon's firing and where
 * every one of them is. A WEAPON's model (skeleton_prop_weapon) carries the
 * muzzle flash under Switches[0] and Switches[2], which the game shows while
 * the carrier fires (propobj.c, weaponSetGunfireVisible): those lists stay the
 * game's, drawn over the item, and are not part of the model's box. While a
 * guard CARRIES the weapon it is not drawn by the object render: chr.c places
 * its model in the guard's hand (chrRenderHeldWeapon) and the guard's own walk
 * draws it after the body, as it draws a worn hat - so the item rides the
 * weapon model's matrix there, and the walk withholds that model's lists but
 * the flash's (THE HELD WEAPON, src/native/sl_chr_asset.c). A weapon the game
 * hides draws nothing either way. Pure. */
#define SL_PROP_GUNFIRE_A           0       /* Switches[0]: the gunfire record */
#define SL_PROP_GUNFIRE_B           2       /* Switches[2]: its flash geometry */
int sl_prop_keeps_gunfire(int kind);
int sl_prop_takes(int kind, int num_switches);

/* THE DESTROYED AUTOGUN. The remaster carries one item for the sentry gun
 * and no destroyed variant, and the console keeps drawing that item once
 * the gun is destroyed, its mount darkened. Destruction is the game's
 * (propobj.c, objExplode): the object's state, its damage, its silence and
 * its drops are not touched here, and the only input the presentation
 * reads is objGetDestroyedLevel - 0 intact, 1 destroyed, one more at every
 * four further shots. The game's own destroyed shading is objDeform's: the
 * FIRST display list of the model (sub_GAME_7F04B478, in tree order - the
 * gun's mount, under the root matrix) gets a copy of its vertices, each
 * blackened by chance - nine in ten above the part's mid-height, one in
 * five below - once per destroyed level, and jittered; the lists under the
 * model's other matrices (the barrel and the gun) are left as they are.
 * The item's vertices are its own, which the game never sees, so the item
 * takes the SHARE objDeform's chances leave: every vertex it binds to the
 * matrix the deformed list is drawn under (`destroyed_mtx`, the heaviest
 * skin lane deciding; every vertex of a one-joint item) draws its colour
 * scaled by 0.1^L when it lies at or above the part's mid-height in MODEL
 * space and by 0.8^L below it - the game's upper half goes black, its lower
 * half dims - and the rest of the item as it was. The mid-height is the
 * middle of the riding vertices' range (objDeform's ymid over the list's
 * own vertices), measured along the model's y through the joint's relation
 * (sl_prop_destroyed_height: a quarter turn of the map turns the axis with
 * it; the scale, positive, is left out of a comparison). The game's
 * jitter, shrink and the upside-down and small-explosion cases are not
 * carried: the item keeps its shape. A destroyed object of any other kind
 * draws as the game's, as before (sl_prop_destroyed_keeps). Renderer side,
 * pure. */
int sl_prop_destroyed_keeps(int kind);
int sl_prop_destroyed_shade(int level, int above); /* 0 .. 255; 255 at level 0 */
float sl_prop_destroyed_height(const struct sl_chr_map *map, float x, float y);
int sl_prop_destroyed_vertex(const void *asset, const struct sl_chr_map *map,
                             unsigned vertex, int mtx);

/* THE WORN HAT'S SEAT. The game places a worn hat on the body's hat node and
 * then moves and sizes it by the per-head entry of its hat table
 * (chr.c, headHat_array_8003E464): the hat's matrix is the node's, times
 * T(21.3 x offset) x S(size). The entries were fitted to the cartridge's
 * heads. The remaster's executable carries the same table, but its hat code
 * applies the entry only while one of its settings is on; otherwise it
 * places the hat on the node with no entry. Its hat and head items are
 * authored in one skeleton space, and measured over every head they fit
 * that way and not with the entry (THE WORN HAT in docs/world-assets.md).
 *
 * So when a hat item is drawn on a head item, the entry is taken back:
 * `m` (row-major, a point drawn as v * m) becomes the node's own matrix,
 * S(1 / size) then T(-21.3 x offset) applied ahead of it, and the hat is then
 * raised by `rest` along the node's y (sl_hat_rest). `seat` is the entry as
 * the game read it: offset x, y, z, size x, y, z. Returns 0 and leaves `m`
 * alone when the entry has no size (none recorded) or a size that is not
 * positive. Renderer side, pure. */
#define SL_HAT_SEAT_UNIT 21.3f
int sl_hat_unseat(float m[16], const float seat[6], float rest);

/* THE HAT'S REST on one head item. On the node alone some of the remaster's
 * heads come up through the remaster's cap (measured over every head: up
 * to 25 game units at the 98th percentile of the cap's footprint, for the
 * fullest hair). The rest is how far the hat must rise on that node for
 * the head to stay under it: over the cap's footprint seen from above, the
 * head's top surface minus the cap's, at the SL_HAT_REST_QUANTILE of the
 * cells both cover; 0 when the head already stays under the cap.
 *
 * Both items are placed in the hat node's frame, in game units: a head
 * vertex v at (v - head_bind) / head_scale (the head map's joint on the
 * node's matrix), a hat vertex c at (c - hat_bind) / hat_scale + hat_origin
 * (the prop map and the model's root origin). `head` and `hat` are the
 * provider's items (struct sl_wa_asset). Returns the rest, never negative;
 * 0 when either item has no geometry over the other. Renderer side, pure. */
#define SL_HAT_REST_QUANTILE 0.98f
float sl_hat_rest(const void *head, const float head_bind[3], float head_scale,
                  const void *hat, const float hat_bind[3], float hat_scale,
                  const float hat_origin[3]);

/* =========================================================================
 * THE DYNAMIC DOOR (#71) - a door the game RESHAPES as it moves, drawn from
 * the presentation's artwork while following the game's own door state.
 *
 * THE GAME'S CONTRACT (propobj.c, measured on the Dam's yard gate). A sliding
 * or vertical door carrying DOORFLAG_CLIP_TO_BBOX moves under its matrix by
 * openPosition times its travel, and doorUpdateBbox shrinks the door's box in
 * MODEL space by the same fraction - xmin rises toward xmax (sliding), ymax
 * falls toward ymin (vertical). That box is the door's collision
 * (sub_GAME_7F03F540 builds the collision polygon from it under the moved
 * matrix), and doorBuildClippedVertices clamps every model vertex past the
 * box's moving face onto it, rebuilding its S/T along the quad's edge. So the
 * part of the door that has slid past the jamb is not drawn, the part still
 * in the opening is drawn exactly where it collides, and the far end cap
 * rides the moving face. The door's state - openPosition, its box, its
 * collision, its timing - is the game's alone.
 *
 * THE PRESENTATION'S CONTRACT. The visible door follows the game's box and
 * nothing else: the half-space the box keeps (sl_door_cut_of, from the live
 * box and the door type) is what is drawn, in every presentation.
 *
 *   ORIGINAL and COMMUNITY HD draw the game's own lists, reshaped by the
 *   game's own clamp, with their own artwork (COMMUNITY HD's per texture,
 *   the texture provider's fallback). Those lists are bracketed by
 *   SL_DOOR_DL_W0 (w1 = 1 open, 0 close) so the renderer keeps them closed
 *   at the eye: a triangle the near plane cuts draws its sub-near part at
 *   the near depth instead of opening the view behind the door.
 *
 *   A presentation with an item for the model (class PROP, prop/<model>,
 *   related to it by sl_prop_derive) draws the item instead - every one of
 *   the model's lists withheld - cut at the same half-space
 *   (sl_door_cut_mesh): triangles across the face are clipped, attributes
 *   interpolated, and what lies wholly past it is laid onto the face (the
 *   game's clamp, generalised past quads), so the item's far end closes the
 *   door where the game's does. The item is drawn with the rigid props'
 *   near-cut fill.
 *
 * THE RIGID DOOR. A sliding or vertical door WITHOUT DOORFLAG_CLIP_TO_BBOX is
 * not reshaped: the game moves its whole model under its matrix and leaves
 * the box alone (doorBuildClippedVertices is never reached). An item for its
 * model rides that matrix whole, exactly as a rigid prop's item does - no
 * cut, every list of the model withheld. Without an item the game's lists
 * draw as they always did. Facility uses one model, the gas-plant door, for
 * twelve clipped and three unclipped doors; this is what keeps the three
 * from drawing the cartridge's door beside the item's. A SWINGING door
 * without the flag is the same case: the game turns its whole model about
 * the hinge it puts into the matrix (door7F0526EC, from the door's pad), so
 * its item rides that matrix too - Facility's swing doors and toilet
 * cubicle doors.
 *
 * THE PARTED DOOR (#96). Caverns' airlock doors are EYE (eyelid) and IRIS
 * doors without the flag: the game never reshapes them and keeps the box,
 * but the object render turns their leaves under matrices of their own every
 * frame (propobj.c, the door branch: the eyelid's two leaves about x by the
 * open angle, the iris's six blade holders and six blades about z), at the
 * model's own nodes. An item whose joints name those matrices (the skeleton's
 * names, SKEL_UPPER / SKEL_LOWER, SKEL_OUT1..6 / SKEL_IN1..6) rides them as
 * a moving prop's does (sl_prop_derive) - every list of the model withheld;
 * an item that is one rigid piece is refused and the game's lists draw.
 *
 * THE AZTEC CHAIR (#96). Aztec's boardroom chairs are doors of type 9
 * (DOORTYPE_AZTECCHAIR): the object render turns them about their hinge as it
 * turns a swinging door - door7F0526EC (propobj.c) builds the one matrix for
 * both, the chair about z at the box's xmax where the swing door turns about
 * y - so an item for the chair's model rides that matrix whole, as a rigid
 * door's does.
 *
 * SL_DYNDOOR=0 (D-027) takes the whole contract away: every door draws as
 * the cartridge draws it, in every presentation. */
#define SL_DOOR_FLAG_CLIP_TO_BBOX   0x4
#define SL_DOOR_TYPE_SLIDING        0
#define SL_DOOR_TYPE_VERTICAL       4
#define SL_DOOR_TYPE_SWINGING       5
#define SL_DOOR_TYPE_EYE            6
#define SL_DOOR_TYPE_IRIS           7
#define SL_DOOR_TYPE_AZTECCHAIR     9
#define SL_DOOR_DL_W0               0x02534452u   /* 0x02 'S' 'D' 'R' */
/* Does the dynamic-door contract take a door of these doorFlags / doorType?
 * CLIP_TO_BBOX on a sliding or vertical door. Pure. */
int sl_door_takes(int door_flags, int door_type);
/* Does an item for this door's model ride its matrix whole (THE RIGID DOOR)?
 * No CLIP_TO_BBOX, on a sliding, vertical, swinging or Aztec-chair door.
 * Pure. */
int sl_door_rides(int door_flags, int door_type);
/* Does an item for this door's model ride its moving parts (THE PARTED
 * DOOR)? No CLIP_TO_BBOX, on an eye or iris door. Pure. */
int sl_door_parts(int door_flags, int door_type);
/* Which draw a door gets this frame: the cartridge's (the contract off, or a
 * door it does not take), the game's own lists bracketed, or the item cut -
 * `item_ready` meaning the presentation in force carries an item for the
 * model that rides it (sl_door_item_fits, sl_prop_derive) and the door is
 * undamaged. Decided afresh at every draw from those inputs alone, so a
 * presentation switch changes the draw at the next frame and nothing else.
 * Pure. */
#define SL_DOOR_PATH_GAME    0
#define SL_DOOR_PATH_NATIVE  1
#define SL_DOOR_PATH_ITEM    2
int sl_door_path(int contract_on, int takes, int item_ready);
/* The half-space of MODEL space a door's live box keeps: axis 0 (x) or
 * 1 (y), keep where sign * (coordinate - value) >= 0. `box` is the game's
 * live bbox, xmin xmax ymin ymax zmin zmax. 1, or 0 for a type the contract
 * does not take. Pure. */
int sl_door_cut_of(int door_type, const float box[6], struct sl_door_cut *out);
/* Can the renderer cut this item (sl_door_cut_mesh): a class-PROP item of
 * one rigid joint whose vertices and indices fit SL_DOOR_MAX_VERTS /
 * SL_DOOR_MAX_INDICES as the cut may grow them? Renderer side, pure. */
#define SL_DOOR_MAX_VERTS           8192
#define SL_DOOR_MAX_INDICES         16384
int sl_door_item_fits(const void *asset);

/* =========================================================================
 * THE FIRST-PERSON VIEW MODEL (#70) - the player's weapon and the hand that
 * holds it, drawn from a presentation's replacement item.
 *
 * THE CONTRACT. The game draws the first-person weapon from ONE model file
 * per weapon, G<item>Z, which carries the gun and the hand together
 * (gunfire.c gunRenderFirstPersonGunModels); the remaster's item is the same
 * unit. When the presentation carries the item (class GUN, "gun/<item>", the
 * model's own name lowered), the game's own lists of that model are not
 * emitted - all of them but the muzzle flash under the SW_FLASH switch, whose
 * images are the game's own in the item too - and the item is drawn in their
 * place, in the pose the game's view-model code wrote this frame. The
 * weapon's state, animation, timing and every switch stay the game's: the
 * item's draws are gated by the game's own switches (the hand by
 * SW_HANDOFFn), and its parts ride the game's own matrices (the trigger
 * finger, the slide). A weapon with no item, or one refused, draws whole as
 * the game's - there is no partial unit.
 *
 * THE BRIDGE: one command at the head of the model's geometry,
 *
 *   w0 = 0x02 'S' 'V' 'W'        SL_VM_DL_W0
 *   w1 = the struct sl_vm_draw   owned by the seam's ring (sl_vm_draw_ok)
 * ========================================================================= */
#define SL_VM_DL_W0           0x02535657u
#define SL_VM_DRAW_MAGIC      0x564D4452u   /* "VMDR" */
#define SL_CHR_CLASS_GUN      5u            /* SL_WA_CLASS_GUN */
#define SL_VM_MAX_SWITCH      64
#define SL_VM_MAX_MTX         64          /* SL_WA_MAX_PART_MATRIX */

/* The game view model's positioned parts: the root GROUP, and every node of
 * the model file's Switches[] table the game positions a part with (a GROUP
 * or GROUPSIMPLE), each with its origin in MODEL space (the GROUP origins of
 * its ancestors and itself summed) and the matrix its geometry is drawn
 * under. */
struct sl_vm_parts {
    float root_origin[3];
    int   root_mtx;
    int   count;                              /* Switches[] entries read */
    unsigned char present[SL_VM_MAX_SWITCH];
    float origin[SL_VM_MAX_SWITCH][3];
    int   mtx[SL_VM_MAX_SWITCH];
    int   mtx_count;                          /* the model's numMatrices */
    /* every GROUP / GROUPSIMPLE node by the matrix it is drawn under, its
     * origin in MODEL space (THE WATCH HAND) */
    unsigned char node_present[SL_VM_MAX_MTX];
    float node_origin[SL_VM_MAX_MTX][3];
};

struct sl_vm_draw {
    unsigned magic;                          /* SL_VM_DRAW_MAGIC */
    const void *asset;                       /* the provider's item */
    const struct sl_chr_map *map;
    const void *mtx;                         /* the model's matrices (N64 Mtx) */
    int   mtx_count;
    unsigned char shade[4];                  /* the view model's shade register */
    unsigned switch_visible[2];              /* bit k: Switches[k] visible */
    int   alternatives;                      /* THE WATCH HAND: SL_VM_ALT_* */
    int   release_depth;                     /* THE WATCH HAND: what the game draws
                                                after the arm is not cut by it */
    int   fade;                              /* THE WEAPON ON THE WATCH: 255 opaque,
                                                below it translucent by that much */
};

/* Renderer side (src/gfx/sl_gfx_charskin.c). Relate a view-model item to the
 * game's view model: every item joint to the part its record names (the root
 * joint to the model's root), at one common scale, the item's bind
 * orientation being the game's own. 1 and *out filled (rot 0 throughout), or
 * 0 with the reason in `why` (at least 96 bytes). Pure.
 *
 * An item whose joints name the model's MATRICES (THE WATCH HAND below) is
 * related by those joints alone: the first of them is the anchor, the scale
 * is the ratio of the others' distances from it, item against model, and ONE
 * quarter turn about z - the one every such joint then sits within
 * SL_CHR_SCALE_TOL of its node under - turns the item into the model's frame
 * (rot, the same for every joint). A joint named by nothing takes no matrix,
 * and no vertex may be bound to it. */
int sl_vm_derive(const void *asset, const struct sl_vm_parts *parts,
                 struct sl_chr_map *out, char *why);

/* Is mesh `flags` (a class-GUN mesh's switch conditions) drawn under this
 * switch visibility? Pure. */
int sl_vm_mesh_visible(unsigned flags, const unsigned switch_visible[2]);

/* THE COSTUME. The game dresses a first-person arm in the player's costume:
 * the watch gadgets' models carry one cuff per costume under Switches[29..34]
 * (SW_BOILER .. SW_SNOW), of which gunfire.c (bondviewSelectCuff(model,
 * header, 0x1D)) shows the one the stage's costume names. An item that
 * stands for such a model must say which of its draws belongs to which
 * costume - its draws gated by those switches - or it would draw every
 * costume at once. Does any draw of `asset` stand under a switch in
 * [first, first + count)? Pure. */
#define SL_VM_CUFF_FIRST   29
#define SL_VM_CUFFS         6
int sl_vm_item_gates(const void *asset, int first, int count);

/* THE WATCH HAND. The watch is worn on a model of its own, the left arm
 * Csuit_lf_handZ, drawn by bondview2.c bondviewRenderWatch while the watch
 * is up: the arm, the hand, the watch and its three hands (Switches[0..2],
 * turned by the level's clock), the dial face (Switches[3], off while a
 * watch page is shown) and one cuff per costume under Switches[4..9], of
 * which bondviewSelectCuff(model, header, 4) shows the one the player's
 * costume names. The presentation's item for it is "gun/suit_lf_hand"
 * (class GUN, the model's own name lowered): the arm and hand with the
 * watch, its joints the model's wrist and watch hands by matrix, and the
 * source's alternative sleeves, each marked SL_WA_MESH_ALT and gated on the
 * cuff the preparation derived it serves (tools/worldasset/watchhand.py).
 *
 * THE ALTERNATIVES (sl_vm_draw_visible). The source draws every sleeve at
 * once; how many are drawn here is the draw's `alternatives`:
 *
 *   SL_VM_ALT_ONE   (default) a sleeve is drawn while the cuff it serves is
 *                   the game's visible one, and never when it serves none -
 *                   exactly one sleeve, the costume's;
 *   SL_VM_ALT_ALL   every sleeve, whatever it serves: the source's own
 *                   composite, all of them fighting by depth.
 *
 * A costume whose cuff no sleeve serves draws the game's whole watch hand
 * (no partial arm): the seam asks sl_vm_alt_serves before it replaces
 * anything. The mode comes from SL_WATCH_SLEEVES (one | all | native;
 * native keeps the game's watch hand under every presentation). Pure. */
#define SL_VM_ALT_ONE       0
#define SL_VM_ALT_ALL       1
#define SL_VM_WATCH_CUFF_FIRST  4
#define SL_VM_WATCH_CUFFS       6
int sl_vm_draw_visible(unsigned flags, const unsigned switch_visible[2], int alternatives);
/* Does any alternative of `asset` serve game switch `sw`? Pure. */
int sl_vm_alt_serves(const void *asset, int sw);

/* A STATIC view model: a weapon item with no skeleton, one rigid piece on
 * one root joint (the Skorpion's). It is related to the game's view model
 * as a prop is - by the reference box its bundle carries against the box of
 * the view model's own vertices (sl_prop_derive, THE STAGE'S RIGID PROPS
 * below) - and rides the model's root matrix. Pure. */
int sl_vm_item_static(const void *asset);

/* THE WEAPON ON THE WATCH. The watch's pages show a weapon: the mission
 * page the one in hand, the equipment page the highlighted one. The game
 * draws it from the SAME first-person model file, G<item>Z, loaded into the
 * right hand's buffer (gunfire.c set_enviro_fog_for_items_in_solo_watch_menu,
 * called from options.c draw_current_hand_item_and_ammo and the equipment
 * page), posed by the watch's own matrix and projection with the Z buffer
 * off. So the item that stands for the held weapon stands for it here too,
 * the whole weapon as the unit, through the same map; the seam's entry is
 * sl_vm_menu_begin, keyed by the item the watch passes.
 *
 * THE LOOK is the game's render data for that draw (model.c's render modes
 * for gun models). The mission page draws with the first-person mode, the
 * fog register = envcolour (the watch's green); the equipment page, and a
 * page fading in, with the TRANSLUCENT one: PropType 5, fog = fogcolour and
 * the surface's alpha = envcolour's low byte. sl_vm_menu_look turns those
 * registers into the bridge's shade and fade. Pure. */
#define SL_VM_LOOK_TRANSLUCENT_TYPE  5      /* model.c: PROP_TYPE_WEAPON+1 */
void sl_vm_menu_look(int prop_type, unsigned envcolour, unsigned fogcolour,
                     unsigned char shade[4], int *fade);

/* Native side (src/native/sl_vm_asset.c). */
int sl_vm_draw_ok(const void *p);

#endif /* SL_CHR_ASSET_H */
