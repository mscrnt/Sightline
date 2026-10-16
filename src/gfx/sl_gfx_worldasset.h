/**
 * sl_gfx_worldasset.h - the WORLD-ASSET PROVIDER (#68): an OPTIONAL, ADDITIVE,
 * RENDER-ONLY external world asset, identified by ASSET CLASS and CANONICAL
 * ITEM PATH, and the one seam that answers for it.
 *
 * THE CONTRACT, in one sentence: the renderer asks "is there an external world
 * asset of class C for item path P?" and gets either NULL - draw exactly what
 * the accepted build draws, with no lookup having cost anything - or a small
 * validated bundle of vertices, indices, meshes, materials and item-local
 * images to draw IN ADDITION to the game's own picture. Nothing about the
 * simulation, the display list or the game's own geometry moves.
 *
 * WHY THIS IS NOT THE TEXTURE PROVIDER (#47). That provider answers "what
 * artwork goes on THIS game texture", keyed by the N64 texture number, and it
 * can only ever repaint a surface the game already draws. The first asset this
 * one carries has NO counterpart in the game at all: it is additive geometry,
 * and no per-texture substitution can produce it at any resolution. So the
 * identity is different in kind - a class and an item path, never a texture id
 * and never a mesh index - and the two providers remain independent
 * MECHANISMS: different identity, different root, different format, different
 * cache, neither reading the other's state.
 *
 * WHAT CHANGED ON 2026-09-23, and it is not that. The two providers are no
 * longer SELECTED independently. The player has one visual selection -
 * PRESENTATION ORIGINAL / COMMUNITY HD / XBLA - and a policy
 * (src/platform/sl_presentation.h) resolves it into a set for each provider,
 * so one F5 press moves both. This file asks that policy which set is
 * selected; it still does not read the texture provider's setting, and the
 * texture provider still does not read this one. Independent mechanisms,
 * coordinated selection.
 *
 * IDENTITY. (class, path), both from the game's own vocabulary:
 *
 *     SL_WA_CLASS_SKYDOME + "dam"
 *
 * The class says what the asset IS and therefore how it is drawn; the path
 * says which item it belongs to. A stage with no asset of that class looks up
 * nothing and draws exactly as before, which is what makes a non-Dam stage a
 * negative control rather than a leap of faith.
 *
 * THE ROOT. Never a compiled-in path, and never a source tree:
 *
 *   1. $SL_WORLDASSET_ROOT, if set and non-empty (tests, development launches
 *      - tools\windows\play.ps1 hands the checkout's own generated root here)
 *   2. <asset override dir>\worldassets - the player-data ladder every other
 *      native asset already uses
 *
 * under which an asset is one file per (class, path):
 *
 *   <root>\xbla\<class>\<path>.slwa
 *
 * A root that does not exist, a class folder that does not exist and a file
 * that is not there are all the same outcome: MISSING, the original picture,
 * one line in the log, and the game plays.
 *
 * ACTIVATION is separate from installation and defaults OFF. Since
 * 2026-09-23 it follows the PRESENTATION (sl_presentation.h): ORIGINAL and
 * COMMUNITY HD select no world assets at all - no root resolved, no directory
 * opened, not one file touched - and XBLA selects the "xbla" set.
 * $SL_WORLD_ASSETS is no longer the selection; it is an ADVANCED / DIAGNOSTIC
 * override that PINS this one provider ("xbla" lit, anything else dark) ahead
 * of the presentation, and it announces itself on stderr whenever it
 * disagrees with what the presentation selected, so a stale variable cannot
 * quietly make a session look mixed. The normal launch never sets it.
 *
 * THE RUNTIME FORMAT ("SLWA"). The tree links no model loader and none is
 * added (project rules: no dependency without asking) - and the runtime does
 * not parse the private source container either. A preparation tool converts
 * the user's own source, once, outside the engine, into this trivially parsed
 * Sightline-owned form, and the runtime reads only this. Little-endian
 * throughout, 128-byte header, every section at a 4-byte-aligned offset:
 *
 *   +0    'S' 'L' 'W' 'A'
 *   +4    u32 version          SL_WA_VERSION
 *   +8    u32 asset_class      SL_WA_CLASS_*
 *   +12   char path[32]        canonical item path, NUL-padded; the file's own
 *                              name must agree with it
 *   +44   u32 matx_off         version 1, 2: 0 (reserved; non-zero is
 *                              rejected).  version 3: the material-extension
 *                              table, material_count * 96 bytes, never 0
 *   +48   f32 bounds_min[3]    the asset's own AABB, as prepared
 *   +60   f32 bounds_max[3]
 *   +72   u32 vertex_count     1 .. SL_WA_MAX_VERTICES
 *   +76   u32 index_count      3 .. SL_WA_MAX_INDICES, a multiple of 3
 *   +80   u32 material_count   1 .. SL_WA_MAX_MATERIALS
 *   +84   u32 texture_count    0 .. SL_WA_MAX_TEXTURES
 *   +88   u32 mesh_count       1 .. SL_WA_MAX_MESHES
 *   +92   u32 vertex_off       vertex_count * 24 bytes
 *   +96   u32 index_off        index_count * 4 bytes
 *   +100  u32 material_off     material_count * 16 bytes
 *   +104  u32 texture_off      texture_count * 16 bytes
 *   +108  u32 mesh_off         mesh_count * 16 bytes
 *   +112  u32 file_size        exactly the file's length
 *   +116  u32 aux_off          version 1: 0.  version 2: 0, or the per-vertex
 *                              auxiliary section, vertex_count * 12 bytes.
 *                              version 3: the section, vertex_count * 24
 *                              bytes, never 0
 *   +120  u32 anim_off         version 1: 0.  version 2, 3: 0, or the
 *                              animation table, anim_count * 128 bytes
 *   +124  u32 anim_count       version 1: 0.  version 2: 0 .. SL_WA_MAX_ANIMS,
 *                              non-zero exactly when aux_off is.  version 3:
 *                              the same range, non-zero exactly when
 *                              anim_off is
 *
 *   vertex   (24) f32 x,y,z   f32 u,v   u8 r,g,b,a
 *   index    (4)  u32, < vertex_count
 *   material (16) u32 texture (< texture_count)  u32 blend (0 opaque,
 *                 1 straight alpha, 2 cutout)  u32 flags (SL_WA_MAT_*)
 *                 u32 reserved (0)
 *   texture  (16) u32 w,h (1 .. SL_WA_MAX_TEX_DIM)  u32 data_off
 *                 u32 data_len (== w*h*4, RGBA8, rows top first)
 *   mesh     (16) u32 material (< material_count)  u32 first_index
 *                 u32 index_count (a multiple of 3, first+count <= total)
 *                 u32 flags (0)
 *   aux      (12) s16 wave_a, wave_b   s16 nx, ny, nz (32767 = 1.0)
 *                 u16 anim (0 = static; else 1 .. anim_count)   see below
 *         v3 (24) ... then s16 xx, xy, xz   s16 yx, yy, yz (32767 = 1.0):
 *                 the tangent frame, see MATERIAL EXTENSION below
 *   anim    (128) u32 mode (SL_WA_ANIM_SINEWAVES)  f32 coord_scale (> 0)
 *                 u32 wave_count (1 .. SL_WA_ANIM_WAVES)  u32 0
 *                 f32 amplitude[4] phase[4] speed[4] freq_a[4] freq_b[4]
 *                 f32 turn_scale turn_bias turn (!= 0)  f32 0   f32 poly[4]
 *   matx     (96) u32 mode (SL_WA_MATX_*)  u32 normal_tex, sphere_tex,
 *                 offset_tex (< texture_count when mode is not NONE)
 *                 u32 flags (SL_WA_MATX_F_*)  f32 base_weight[3]
 *                 f32 reflect_weight[3]  f32 warp_scale  f32 normal_scale
 *                 (!= 0)  f32 normal_bias  f32 sphere_scale[2]
 *                 f32 sphere_bias[2]  f32 mean_weight  f32 alpha_floor
 *                 u32 0 x4.   A NONE record is all zeros.
 *
 * VERSION 2 (#69) is version 1 plus the animation words: a bundle written
 * before them carries zeros there and is a version-1 file, and this parser
 * reads both. A bundle with nothing to animate is still written at version 1
 * - the same bytes as before - so the two versions differ exactly where a
 * file carries motion. See MATERIAL ANIMATION below for what the records
 * mean.
 *
 * VERSION 3 (#69 material) is version 2 with the auxiliary entry widened to
 * carry the tangent frame and the header word at +44 naming the
 * material-extension table. A bundle that carries no material record is
 * still written at version 2 (or 1) - the same bytes as before - so the
 * versions differ exactly where a file carries a material program. See
 * MATERIAL EXTENSION below.
 *
 * THIS IS NOT A UNIVERSAL MODEL FORMAT and is not offered as one. It carries
 * exactly what the first slice's renderer consumes and rejects everything
 * else, so that what it does carry can be validated exhaustively.
 *
 * SAFETY. Every count is bounded BEFORE it is multiplied and before a byte is
 * allocated; every section is required to lie wholly inside a file whose
 * length the header itself declared and which is checked against the real
 * length; sections may not overlap each other or the header; every index,
 * material reference and texture reference is range-checked at load, not at
 * draw. A malformed, truncated or unsupported file is INVALID - counted,
 * reported once, and the stage draws exactly as it does with no asset at all.
 * There is no path by which a bad file becomes a fault.
 *
 * HOST-CLEAN: plain C types only, no game and no GL headers, so src/gfx
 * compiles it and the self-test (-DSL_WA_SELFTEST) links it alone.
 */
#ifndef SL_GFX_WORLDASSET_H
#define SL_GFX_WORLDASSET_H

#include <stdio.h>

#define SL_WA_MAGIC0 'S'
#define SL_WA_MAGIC1 'L'
#define SL_WA_MAGIC2 'W'
#define SL_WA_MAGIC3 'A'
#define SL_WA_VERSION      4u   /* the newest layout this parser reads */
#define SL_WA_VERSION_MIN  1u   /* ... and the oldest; all four are accepted */
#define SL_WA_HEADER  128u
#define SL_WA_PATH_MAX 32u

/* Asset classes. The class decides how the renderer draws the thing, so an
 * unknown one is rejected rather than drawn as something else.
 *
 * SKYDOME is ADDITIVE and EYE-ANCHORED: it is drawn as well as the game's own
 * picture, its centre is put on the recovered eye and it carries no parallax.
 *
 * BACKGROUND is a REPLACEMENT and WORLD-ANCHORED: it stands in for the stage's
 * own static background geometry, it sits where that geometry sits through
 * room changes and portal traversal, it is depth-tested like world geometry,
 * and the original emission it replaces is suppressed - but ONLY once this
 * asset is loaded, validated and uploaded. None of the skydome's anchoring
 * applies to it; see SL_WA_WORLD_SCALE.
 *
 * CHAR and HEAD (#70) are REPLACEMENTS of one of the game's own character
 * models - a body (the game's C<item>Z) or a head (C<item>Z of a head) - and
 * they are SKINNED: drawn deformed by the game's own skeleton, every frame,
 * in the pose the game animated. They are version-4 files and only they are;
 * see SKINNED ITEMS below and src/gfx/sl_gfx_charskin.h. */
#define SL_WA_CLASS_NONE        0u
#define SL_WA_CLASS_SKYDOME     1u
#define SL_WA_CLASS_BACKGROUND  2u
#define SL_WA_CLASS_CHAR        3u
#define SL_WA_CLASS_HEAD        4u
#define SL_WA_CLASS_GUN         5u   /* #70: a first-person view model */
#define SL_WA_CLASS_PROP        6u   /* #71: a rigid prop model's replacement */

/* Record sizes, fixed by the format. */
#define SL_WA_VERTEX_SIZE   24u
#define SL_WA_INDEX_SIZE     4u
#define SL_WA_MATERIAL_SIZE 16u
#define SL_WA_TEXTURE_SIZE  16u
#define SL_WA_MESH_SIZE     16u
#define SL_WA_AUX_SIZE      12u   /* version 2 */
#define SL_WA_AUX3_SIZE     24u   /* version 3: the same 12, then the frame */
#define SL_WA_ANIM_SIZE    128u
#define SL_WA_MATX_SIZE     96u

/* SKINNED ITEMS (#70) - VERSION 4, classes CHAR and HEAD only.
 *
 * The version-1 tables unchanged, the header word at +44 zero, and the three
 * words at +116 given the meaning a skinned item needs:
 *
 *   +116  u32 skin_off     the SKIN section, vertex_count * 8 bytes, never 0
 *   +120  u32 joint_off    the JOINT table, joint_count * 48 bytes, never 0
 *   +124  u32 joint_count  1 .. SL_WA_MAX_JOINTS
 *
 *   skin   (8)  u8 joint[4]  u8 weight[4]   up to four influences; every
 *               joint < joint_count, the weights sum to exactly 255, and an
 *               unused lane is joint 0 weight 0
 *   joint  (48) char name[24]   the source's own joint name, NUL-terminated
 *                               inside the field, [A-Z0-9_] only
 *               u32 parent      0xFFFFFFFF for the root, else an EARLIER
 *                               joint - so the table is its own topological
 *                               order
 *               f32 bind[3]     the joint's bind position in item space,
 *                               finite
 *               u32 reserved[2] 0
 *
 * A mesh's flags word, zero in every other version, may carry
 * SL_WA_MESH_BUILTIN_HEAD: the draw is the body's own head, drawn only when
 * the character wears no separate head. Vertex positions are the item's
 * bind space, unscaled - relating it to the game's skeleton is the draw's
 * business (sl_gfx_charskin.h), not the file's. */
#define SL_WA_SKIN_SIZE       8u
#define SL_WA_JOINT_SIZE     48u
#define SL_WA_JOINT_NAME     24u
#define SL_WA_MAX_JOINTS     32u
#define SL_WA_SKIN_LANES      4u
#define SL_WA_JOINT_ROOT     0xFFFFFFFFu
#define SL_WA_MESH_BUILTIN_HEAD 1u
/* SL_WA_MESH_OWN_HEAD (#70): a BODY item with no place to wear a separate
 * head - the source gives it no head attach point and draws the head it
 * carries always (Bond's bodies are such items). Carried on EVERY mesh of such
 * an item or on none, never beside SL_WA_MESH_BUILTIN_HEAD, class CHAR only;
 * the seam lets the item stand for the game's body and the head that body
 * wears (sl_chr_item_owns_head). */
#define SL_WA_MESH_OWN_HEAD     2u

/* VIEW MODELS (#70) - class GUN, the same version-4 layout: a FIRST-PERSON
 * weapon item, the gun and the hand that holds it as one unit, rigidly bound
 * to the game's own view-model skeleton. Two fields carry what only this
 * class needs:
 *
 *   joint reserved[0]   the joint's GAME PART: 0 for a joint the game positions
 *                       no part for (the root rides the model's root), else 1 +
 *                       the index, in the model file's Switches[] table, of the
 *                       node the game positions that part with (FINGER, SLIDE)
 *   mesh flags 8..15,   a SWITCH CONDITION each: 1 + the Switches[] index of a
 *   and 16..23          game switch the draw stands under, 0 for none; the draw
 *                       is drawn only while every switch it names is visible
 *   material +12        0, or 1 + the index of the material's REFLECTION image:
 *                       a metal weapon's environment image (the gold and silver
 *                       pistols, the golden gun, the laser, the knives), laid
 *                       over the draw by the surface's direction to the eye
 *
 * and SL_WA_MESH_BUILTIN_HEAD does not apply to it.
 *
 * PROPS (#71) - class PROP, the same version-4 layout for a replacement of
 * one of the game's RIGID prop models (P<item>Z: a mainframe, a desk, a
 * monitor): one joint, the root, every vertex on it, no part and no switch
 * condition. Its source item has no skeleton, so nothing in the item says
 * where the game's model sits in it. The header's bounds therefore carry
 * the REFERENCE box: the axis-aligned box of the source's ORIGINAL
 * generation of the same item - the game's own model, in the item's space -
 * and the draw relates it to the box of the game model's own vertices
 * (sl_prop_derive).
 *
 * A MOVING prop (a vehicle, whose model turns its wheels under matrices of
 * their own) carries several joints, the reference box all the same: a
 * joint's reserved[0] may be SL_WA_PART_MATRIX + 1 + m, the model matrix the
 * joint rides (never a Switches[] part), and 0 on a joint that rides the
 * model's root.
 *
 * A prop's material +12 may name a REFLECTION image as a view model's does -
 * 0, or 1 + the index of an image of the bundle - and the prop draw lays it
 * the same way (sl_gfx_dl.c chr_reflection: each solid, non-decal draw that
 * names one, again, by the sphere coordinate of the vertex's eye-space
 * direction, multiplied in at twice its value; not on a fading draw or a
 * dynamic door's cut item). The preparation writes it only for the props
 * prepare.PROP_REFLECTIONS names (Egyptian's Golden Gun pickup); every other
 * prop bundle carries 0. The loader refuses a reflection on any class but
 * GUN and PROP, on a bundle without joints, and past the texture count. */
#define SL_WA_MAX_SWITCH         64u
#define SL_WA_MESH_SWITCH_A(f)   (((f) >> 8) & 0xffu)
#define SL_WA_MESH_SWITCH_B(f)   (((f) >> 16) & 0xffu)
#define SL_WA_MESH_SWITCH_BITS   0x00ffff00u

/* THE WATCH HAND (#70) - class GUN as well: the left arm the watch is worn
 * on (the game's Csuit_lf_handZ). Two further values, class GUN only:
 *
 *   joint reserved[0]   SL_WA_PART_MATRIX + 1 + m: the joint IS the model's
 *                       node drawn under matrix m (the wrist, a watch hand)
 *                       - the game positions the arm's parts by its GROUP
 *                       tree, not by Switches[] entries
 *   mesh flags bit 24   SL_WA_MESH_ALT: the draw is one of the item's
 *                       ALTERNATIVE costume pieces (a sleeve). Its switch
 *                       condition, when it has one, is the costume cuff the
 *                       preparation derived it serves; one with none serves
 *                       no costume. The runtime draws the alternatives by
 *                       the rule of sl_vm_draw_visible (src/sl_chr_asset.h). */
#define SL_WA_PART_MATRIX        0x100u
#define SL_WA_MAX_PART_MATRIX    64u
#define SL_WA_MESH_ALT           0x01000000u

/* MATERIAL EXTENSION (#69 material): the source's own pixel-program contract
 * for a surface drawn from four images, carried as data and evaluated here.
 *
 * WHAT IS DRAWN AND HOW, read from the remaster's water pixel program (its
 * microcode, the register names in its own constant table, the constants the
 * item uploads beside each draw and the literal bank stored in front of its
 * code; see tools/worldasset/xblaworld.py, material_record, for each), with
 * `uv` the vertex's texture coordinate, `colour` its colour, and the three
 * per-vertex vectors of the tangent frame - X (the aux entry's xx,xy,xz), Y
 * (yx,yy,yz) and the normal (nx,ny,nz) - each rotated into VIEW space by the
 * camera's rotation and normalised, as the source's vertex program does:
 *
 *   n     = normal_tex(uv) * normal_scale + normal_bias       tangent space
 *   N     = normalize(n.x * X + n.y * Y + n.z * normal)       view space
 *   base  = base_tex(uv + warp_scale * offset_tex(uv).xy)
 *   refl  = sphere_tex(N.x * sphere_scale.x + sphere_bias.x,
 *                      N.y * sphere_scale.y + sphere_bias.y)
 *   rgb   = base_weight * colour.rgb * base.rgb + reflect_weight * refl
 *   a     = max(mean_weight * (rgb.r + rgb.g + rgb.b), alpha_floor)
 *           * base.a * colour.a
 *
 * and then the stage fog the material already consumes (SL_WA_MAT_FOG). The
 * offset image is read UNSIGNED, 0..1, as its fetch constant says; the
 * sphere image clamps to its edge (SL_WA_MATX_F_SPHERE_CLAMP, from the
 * material record's own slot flag) and the others wrap. The program's lit
 * term - ambient plus a directional light on N - is evaluated by the running
 * game with an ambient of one and a light colour of zero on every logged
 * frame, so it is the ambient alone and its specular is zero; no light
 * model is carried. Every number is the item's; this file holds none.
 *
 * WHAT THIS IS FOR. The frame the program is evaluated on MOVES (MATERIAL
 * ANIMATION above) while every texture-space input stands still, so the
 * surface detail this program adds - the warp of the base image and the
 * sphere reflection the normal map steers - rides the displaced geometry;
 * the material carries no time term of its own, and none is invented.
 *
 * The base image is the material's own `texture`; the other three are named
 * by the record. A material whose record is NONE draws exactly as before. */
#define SL_WA_MATX_NONE           0u
#define SL_WA_MATX_NORMAL_SPHERE  1u
#define SL_WA_MATX_F_SPHERE_CLAMP 1u
#define SL_WA_MATX_F_KNOWN        1u

/* MATERIAL ANIMATION (#69): the source's own vertex-program contract for a
 * surface that moves, carried as data and evaluated here.
 *
 * WHAT MOVES AND HOW, read from the remaster's animated-water vertex program
 * (its microcode, its constant table and the constants the running game
 * hands it, all measured; see tools/worldasset/xblaworld.py for each):
 *
 *   per wave i (one register lane, SL_WA_ANIM_WAVES of them):
 *     theta_i = frac( -t * speed_i + phase_i
 *                     + wave_a * coord_scale * freq_a_i
 *                     + wave_b * coord_scale * freq_b_i )
 *     angle_i = (theta_i * turn_scale - turn_bias) * turn
 *     s_i     = poly[0]*angle + poly[1]*angle^3 + poly[2]*angle^5 + poly[3]*angle^7
 *   displacement = sum_i amplitude_i * s_i,   along the vertex's own direction
 *
 * (wave_a, wave_b) is a per-vertex coordinate pair the program reads as an
 * unnormalised integer and scales by its own literal `coord_scale`; the
 * direction is the vertex normal. The polynomial is the program's sine - the
 * item ships its coefficients, and they are evaluated as shipped rather than
 * replaced by a library sine, because the shape of the surface at the turn's
 * ends is part of what the source draws. Every number is the item's; this
 * file holds none of them.
 *
 * TIME. `t` is SECONDS of the game's own frame clock - the VI frame counter
 * the game keeps in frametiming.c, over SL_WA_CLOCK_HZ. Measured on the
 * console the program's time constant starts near zero when gameplay begins,
 * advances by exactly the frame period on every rendered frame, and keeps
 * advancing behind the pause screen; the VI frame counter has the same
 * shape here (the pause zeroes only the simulation's share of the retraces),
 * it is deterministic under a replay, and it is independent of how many
 * frames the window presents. Never the wall clock.
 *
 * LIFETIME. Stateless: the displacement is a pure function of the record,
 * the vertex and the clock, recomputed for the animated vertices on every
 * frame the replacement is drawn, and the bundle's own vertex array is never
 * written. Nothing about the simulation is read but the counter and nothing
 * is written back, so the per-tick trace cannot move. */
#define SL_WA_MAX_ANIMS      64u
#define SL_WA_ANIM_NONE       0u
#define SL_WA_ANIM_SINEWAVES  1u
#define SL_WA_ANIM_WAVES      4u
#define SL_WA_CLOCK_HZ       60u   /* VI frames per second, NTSC */

/* Bounds. Generous enough for a whole level's static background and small
 * enough that every product below fits a 32-bit unsigned with room to spare.
 *
 * THE LAYOUT DID NOT CHANGE when the background class arrived, so the version
 * did not either: a bundle written before it still parses, byte for byte, and
 * the tests check exactly that. What changed is how many of each record the
 * parser will accept, and one more value the class field may take. A wider
 * limit cannot make an older file invalid - it can only stop rejecting a
 * larger new one. */
#define SL_WA_MAX_VERTICES  1000000u
#define SL_WA_MAX_INDICES   3000000u
#define SL_WA_MAX_MATERIALS     256u
#define SL_WA_MAX_TEXTURES      256u   /* Depot's item carries 136 (#92) */
#define SL_WA_MAX_MESHES       1024u
#define SL_WA_MAX_TEX_DIM      2048u
#define SL_WA_MAX_FILE  (192u * 1024u * 1024u)

/* THE WORLD TRANSFORM of a world-anchored asset, and every term of it is READ
 * from the game rather than carried as a constant.
 *
 * WHAT SPACE THE WORLD PROJECTION CONSUMES, measured. GoldenEye builds every
 * room's matrix as (that room's own position TIMES the level scale) MINUS the
 * player's `current_model_pos` (bgroomtrans.c:211-218), so the space the
 * frame's geometry is laid out in is
 *
 *     S = position * level_scale - render_origin
 *
 * and the same relation holds for the camera: at the Dam's gate yard the
 * player's own eye reads (17959.16, 73.15, 17809.95), the render origin reads
 * (17723.85, -107.00, 17612.57), and their difference is (235.31, 180.15,
 * 197.38) - which is where the eye is in S, to the last digit printed.
 *
 * WHAT THE ASSET CARRIES: the source authors a level's bundle in the bg data's
 * OWN room coordinates - the ones BEFORE the level scale. So for a bundle
 * coordinate B the conversion is a scale the game supplies and an offset the
 * game supplies:
 *
 *   FOLDED projection (bondview2.c's, view already in it):  X = B * k - origin
 *   PLAIN projection  (fr.c's; the seam supplies a view
 *     built from the player's view-to-world matrix, whose
 *     translation row is in position*scale units):          X = B * k
 *
 * where k is `room_data_float2` for the stage - 4.2801 at the Dam, and NOT a
 * constant across levels, which is why it is read per frame.
 *
 * A CORRECTION THE SKYDOME ROUND LEFT BEHIND. That round measured
 * `#68 skyeye`'s ratio as "exactly 5.0000" and recorded it as the folded
 * projection consuming world coordinates divided by five. It is an artefact of
 * sl_wa_view_origin below, which recovers the eye ASSUMING the view's basis is
 * orthonormal; the folded view carries a uniform scale of five, so the eye it
 * returns is one fifth of the eye in the matrix's own space. Nothing about the
 * skydome moves - it is eye-anchored and a uniform scale about the eye changes
 * no ray, which is exactly why the error could not show there. A world-anchored
 * asset cannot ignore it, and does not: it takes the scale and the origin from
 * the game and never from the recovered eye.
 *
 * ONE canonical representation in the asset - the source's own - converted at
 * DRAW time into whichever space the projection in force consumes. Nothing is
 * ever pre-folded for whichever room happened to render first. */

/* Material blend modes.
 *
 * CUTOUT is not a third kind of blending, it is the distinction between the
 * two things "this image has an alpha channel" can mean, and keeping them
 * apart is a DEPTH question rather than a colour one:
 *
 *   ALPHA   a genuinely translucent surface - glass, a light shaft, a decal
 *           laid over what is behind it. It must not occlude, so it must not
 *           write depth, and it has to be drawn after what shows through it.
 *           It takes NO ALPHA REJECT: its alpha IS the blend factor, and a
 *           texel at alpha zero already contributes nothing.
 *   CUTOUT  a shape carried in the alpha channel - foliage, a chain fence, a
 *           grating. Wherever it survives the alpha test it is as opaque as
 *           any wall, so it MUST write depth. Drawing it without depth writes
 *           is what lets the world behind it draw straight through it. This
 *           is the ONLY class that alpha-tests: it is drawn opaque, so it
 *           cannot blend its transparent texels away and has to reject them.
 *
 * THE REJECT WAS ON BOTH, AND THAT DELETED SURFACES. The 0.33 threshold
 * entered with the background pass, for the foliage, at a time when foliage
 * reached the ALPHA branch - a draw was translucent if its style said so OR
 * its image carried alpha. The CUTOUT split moved every one of those draws
 * here, and the threshold stayed behind on a class whose whole originating
 * population had left. The source agrees the two are separate: the style
 * word's masked group is the bit 0x0800, set on exactly the 4 foliage draws
 * of the Dam item and on nothing else, and measured over all 386 draws it
 * never co-occurs with the translucent low byte 2 - the two groups are
 * DISJOINT. On that bundle the stranded reject deleted 4 of 19 translucent
 * materials outright, the tunnel's road markings among them at an image alpha
 * maximum of 0.314 and 0.235.
 *
 * The source says which is which and it is READ rather than guessed: a draw
 * whose style low byte is 2 is translucent, and a draw whose style says opaque
 * while its image carries alpha is a cutout. The Dam's background is 22 draws
 * of the first kind and 39050 triangles of the second, so conflating them
 * costs almost the whole of the level's foliage its depth.
 *
 * The LAYOUT is unchanged by this, so the version is too: a bundle written
 * before CUTOUT existed still parses, because this only widens the set of
 * accepted values and can never make an older file invalid. */
#define SL_WA_BLEND_OPAQUE 0u
#define SL_WA_BLEND_ALPHA  1u
#define SL_WA_BLEND_CUTOUT 2u

/* WHICH CLASS TAKES AN ALPHA REJECT. One place, so the renderer's draw loop
 * and the self-test are answering the same question rather than two that
 * happen to agree. It is TOTAL - every blend value gets an answer - which is
 * what says no draw can inherit the previous draw's alpha-test state: the
 * loop installs its own on every mesh, both ways. */
#define SL_WA_BLEND_REJECTS(b)  ((b) == SL_WA_BLEND_CUTOUT)

/* WHICH CLASS BLENDS: the translucent one, and the cutout, whose surviving
 * soft edge is blended too (the draw loop enables GL_BLEND for both). A
 * blended draw laid twice is darker or thicker, so the far pass's overlap
 * is not for it (#80, SL_WA_FAR_OVERLAP_BLEND). */
#define SL_WA_BLEND_BLENDS(b)   ((b) == SL_WA_BLEND_ALPHA || (b) == SL_WA_BLEND_CUTOUT)

/* The reference the CUTOUT class rejects at. Named so the self-test can say
 * which classes it applies to without restating the number. */
#define SL_WA_ALPHA_REF 0.33f

/* Material FLAGS, and the one that exists.
 *
 * SL_WA_MAT_DECAL - this material's draws are the source's own DECAL PASS: a
 * surface authored to lie ON another surface rather than beside it. It is not
 * a kind of blending and it is not a colour property; it is a DEPTH ORDER
 * property, and without it two exactly coincident surfaces resolve per pixel
 * by floating-point noise.
 *
 * WHY IT HAS TO BE CARRIED, measured on the Dam item:
 *
 *   - the overlay and the surface under it are authored EXACTLY coincident.
 *     At playtest mark 1 the bridge decal and the ground beneath it are
 *     0.0000 world units apart; across the eight playtest marks, 652 of 655
 *     rays that cross two surfaces within 0.05 units have exactly one of the
 *     two in the source's decal group.
 *   - which of the two is nearer FLIPS FROM RAY TO RAY. At mark 1 the decal
 *     is in front on 21 rays and behind on 21 others, at a measured
 *     separation of zero. That alternation is the fight, and it is visible
 *     offline in the geometry before any GL state is involved.
 *   - the ordering is already right. The preparation emits draws in the
 *     source's own command-list order and the renderer draws meshes in that
 *     order, so the decal is already issued after its base. Order alone
 *     cannot separate two surfaces at the same depth, which is why this is a
 *     depth contract and not a sorting one.
 *
 * WHAT THE SOURCE CARRIES AND WHAT IT DOES NOT. The item's own vertex
 * programs declare `c_zadjust`, one float4, beside `c_modelviewprojection`, in
 * 36 of its 42 programs - exactly the 36 that declare the projection matrix.
 * So the source's vertex stage takes a per-draw CLIP-SPACE DEPTH ADJUSTMENT by
 * name. Its VALUE is not in the bundle: every constant-upload record in the
 * item's command list was read (189 of them, three distinct shapes, two
 * registers) and none carries a depth term, and no decal-style block uploads a
 * constant at all. So the bundle names the pass and names the mechanism, and
 * the number is the engine's. This flag carries the half that is there.
 *
 * The LAYOUT is unchanged, so the VERSION is too: a bundle written before this
 * flag existed carries zero in that word and still parses, exactly as the
 * CUTOUT widening did. What changes is that the word is no longer required to
 * be zero - and a wider accepted set can never make an older file invalid. */
#define SL_WA_MAT_DECAL    1u

/* SL_WA_MAT_FOG - this material's source pixel program consumes the stage-fog
 * contract: it declares `c_fog` and `c_fogcolour` and ends in
 *
 *     rgb = lerp(c_fogcolour, material colour, saturate(d * c_fog.x + c_fog.y))
 *
 * with d the view-space distance its vertex program writes. Measured on the
 * Dam item: 42 of 42 pixel programs declare both names, and the runtime
 * constants of the ring's program are exactly the SLEN row (fog end, fog
 * start, colour) of the environment in force - see SLEN below. The
 * preparation sets the bit only when EVERY pixel program in the item's
 * constant tables declares both names; it does not decode which program a
 * draw binds, so a split item is left unflagged rather than guessed at. A
 * material without the bit receives no remaster stage fog, which is what
 * every bundle written before the bit existed does. Same layout, same
 * version, a wider accepted set - the DECAL rule. */
#define SL_WA_MAT_FOG      2u

/* SL_WA_MAT_GENERATED - this material is NOT the source's. The preparation
 * made it (tools/worldasset/zenith.py): the D-023 cloud field, grown from the
 * dome image's own cloud band and appended as one more draw after the
 * source's. It is a deliberate divergence from the remaster, whose dome is
 * clear overhead and still, so it is drawn only while that
 * divergence is on - see sl_wa_material_drawn - and with it off the frame is
 * the source dome exactly. Refused on any class but the skydome, whose pass
 * is the only one that consults it. Same layout, same version, a wider
 * accepted set - the DECAL rule. */
#define SL_WA_MAT_GENERATED 4u

/* SL_WA_MAT_COVERAGE_BLUE - this ALPHA-blended material's coverage (its
 * alpha) is its image's BLUE channel, not the image's alpha channel.
 *
 * The source's pixel program decides a blended draw's alpha, and on the
 * remastered items it exports
 *
 *     alpha = coverage * colour.a * max(dot(lit rgb, 1/3), 1)
 *
 * where the last factor is exactly 1 under the game's lighting and `coverage`
 * is a property of the program: the image's alpha on most, nothing (the
 * vertex alpha alone) on some, and on a third family the image's BLUE channel
 * - the fetch never reads the image's alpha at all. That family draws grey
 * glyphs on black from OPAQUE images (a crate stack's graffiti: DXT1 with no
 * transparent block), so black is transparent there and the glyph's grey is
 * its own coverage: a graded edge, no threshold. The draw is otherwise an
 * ordinary blended one - SRC_ALPHA / ONE_MINUS_SRC_ALPHA, alpha test off, no
 * depth write (measured on the running remaster) - which is exactly this
 * renderer's ALPHA material. So the colour equation is unchanged and only
 * the channel alpha is read from moves: the renderer samples alpha from blue
 * for the draw (sl_wa_coverage_blue), leaving the image and its mips as the
 * source has them. The preparation sets the bit from the draw's own program
 * (tools/worldasset/xblaworld.py, coverage_channel), never from a picture.
 *
 * It composes with SL_WA_MAT_DECAL - depth priority and coverage are
 * independent properties of the same draw. Refused on a material that is not
 * ALPHA-blended: an opaque or cutout draw has no coverage of this kind. A
 * bundle written before the bit carries zero and keeps its image's alpha.
 * Same layout, same version, a wider accepted set - the DECAL rule. */
#define SL_WA_MAT_COVERAGE_BLUE 8u

/* SL_WA_MAT_PANORAMA - this material belongs to the item's PANORAMA SHELL: the
 * outermost ring of a background, which lies farther out than everything else
 * in the item and closes the circle round it. The preparation finds it from
 * the geometry alone (tools/worldasset/prepare.py, panorama_shell) - the set of
 * meshes whose nearest vertex is beyond the farthest vertex of every other
 * mesh, surrounding the item's centre - and marks its materials, which must be
 * its own.
 *
 * On the Dam that ring is a painted panorama of peaks and sky some 52000 to
 * 55000 world units out, beyond every stage-fog range the remaster uses in
 * play, so it is drawn at exactly the fog colour and stands between the eye
 * and the sky dome behind it. Under D-026 the part of it that is fully fogged
 * fades toward the sky instead: see sl_wa_panorama_fades and the D-026 rules
 * below. Background class only. Same layout, same version, a wider accepted
 * set - the DECAL rule; a bundle written before the bit carries zero and draws
 * its ring as it always did. */
#define SL_WA_MAT_PANORAMA 16u

/* SL_WA_MAT_CLOUD_PLANE - this GENERATED skydome material is the D-023 cloud
 * field. Its image is ONE REPEAT of the game's own cloud plane (the plane
 * sky.c casts its cloud layer onto), so its texture coordinates are not read
 * from the bundle: each vertex is cast onto that plane from the game's eye,
 * with the game's cloud phase, by sl_wa_cloud_coord - the clouds stand where
 * the game's stand and move as the game's move. Refused on any class but the
 * skydome and without SL_WA_MAT_GENERATED. Same layout, same version, a wider
 * accepted set - the DECAL rule; a bundle written before the bit carries zero
 * and draws its stored coordinates. */
#define SL_WA_MAT_CLOUD_PLANE 32u

/* SL_WA_MAT_CULL_BACK (#96) - this background material's draws stand under the
 * source's FACE CULL (the render-state register tools/worldasset/xblaworld.py
 * calls RS_CULL, written nonzero before 102 of Control's draws and before no
 * draw of any other accepted stage's item): the remaster draws such a triangle
 * only from the side its authored normal, cross(B - A, C - A), faces. Measured
 * in the private emulator on Control's intro swirl, where the game's camera
 * stands behind the lift's back wall: the remaster shows the lift's inside
 * through that wall, and its Caverns intro, whose item carries no cull, shows
 * the wall's back at the same moment. The background pass withholds the
 * triangles of such a material that turn away from the frame's eye in the
 * item's coordinates (sl_wa_cull_drops); SL_BG_CULL=0 draws them as before.
 * Background class only. Same layout, same version, a wider accepted set - the
 * DECAL rule; a bundle written before the bit carries zero and draws both
 * sides as it always did. */
#define SL_WA_MAT_CULL_BACK 64u

/* SL_WA_MAT_GAME_WATER (D-048) - this background material's draws redraw the
 * game's MOVING WATER: they lie on the game's room surfaces of texture 1508,
 * which tex.c draws under MipMap2C_Something_Setup (unk_092E50.c) - two
 * wrapping 64-texel tiles of the image whose origins the water controller
 * sub_GAME_7F092E50 advances every tick, blended by its animated
 * PRIM_LOD_FRAC - and they carry the game's own coordinates there in the
 * remaster's convention, u = s / 2048 + c and v = c' - t / 2048
 * (tools/worldasset/gameshade.py water_draws measures every triangle; Caverns'
 * two water draws, 41 triangles, agree to 0.1%). Nothing in the remaster's
 * records moves them, so the background pass draws such a material through
 * the game's own two-tile blend at the game's phases (sl_wa_water_shift,
 * sl_wa_water_frac); SL_BG_WATER=0 draws the stored coordinates as before.
 * Background class only. Same layout, same version, a wider accepted set - the
 * DECAL rule; a bundle written before the bit carries zero and draws still
 * water as it always did. */
#define SL_WA_MAT_GAME_WATER 128u
#define SL_WA_MAT_KNOWN    255u  /* every bit this version defines */

/* Parse reasons (0 = OK). */
#define SL_WA_OK           0
#define SL_WA_E_MAGIC      1
#define SL_WA_E_VERSION    2
#define SL_WA_E_CLASS      3
#define SL_WA_E_PATH       4
#define SL_WA_E_FLAGS      5
#define SL_WA_E_COUNT      6   /* a count is zero or past its limit */
#define SL_WA_E_LAYOUT     7   /* an offset is unaligned, outside, or overlaps */
#define SL_WA_E_SIZE       8   /* file_size disagrees with the real length */
#define SL_WA_E_INDEX      9   /* an index is >= vertex_count */
#define SL_WA_E_MESH      10   /* a mesh range or material reference is bad */
#define SL_WA_E_MATERIAL  11   /* a material's texture reference or mode is bad */
#define SL_WA_E_TEXTURE   12   /* a texture's size or payload is bad */
#define SL_WA_E_IO        13
#define SL_WA_E_MEM       14
#define SL_WA_E_TRUNC     15   /* fewer bytes than the header needs */
#define SL_WA_E_ANIM      16   /* an animation record or aux entry is bad */
#define SL_WA_E_MATX      17   /* a material-extension record is bad */
#define SL_WA_E_SKIN      18   /* a skin entry or joint record is bad (v4) */

const char *sl_wa_strerror(int rc);

/* ---- the parsed bundle ---------------------------------------------------
 *
 * Every pointer is into ONE allocation the provider owns and keeps for the
 * life of the process (a world asset is loaded at most once per run). */

struct sl_wa_vertex {          /* the file's layout, byte for byte */
    float x, y, z;
    float u, v;
    unsigned char r, g, b, a;
};

struct sl_wa_material {
    unsigned texture;
    unsigned blend;
    unsigned flags;                /* SL_WA_MAT_*; 0 on a bundle written
                                    * before any flag existed */
    unsigned reflect;              /* class GUN or PROP (a bundle with joints)
                                    * only: 0, or 1 + the index of the
                                    * REFLECTION image, below the texture
                                    * count (VIEW MODELS; PROPS) */
};

struct sl_wa_texture {
    unsigned w, h;
    const unsigned char *rgba;     /* w * h * 4, rows top first */
};

struct sl_wa_mesh {
    unsigned material;
    unsigned first_index;
    unsigned index_count;
    unsigned flags;                /* SL_WA_MESH_*; 0 below version 4 */
};

struct sl_wa_skin {            /* the file's layout, byte for byte (#70) */
    unsigned char joint[SL_WA_SKIN_LANES];
    unsigned char weight[SL_WA_SKIN_LANES];   /* sum 255 */
};

struct sl_wa_joint {           /* one joint record, host order (#70) */
    char  name[SL_WA_JOINT_NAME];
    int   parent;              /* -1 for the root */
    float bind[3];
    unsigned part;             /* class GUN: the game part (see VIEW MODELS),
                                  0 on every other class */
};

struct sl_wa_vaux {            /* the file's layout, byte for byte (#69) */
    short wave_a, wave_b;      /* the program's coordinate pair, raw     */
    short nx, ny, nz;          /* the displacement direction, 32767 = 1  */
    unsigned short anim;       /* 0, or 1 + the record that moves it     */
};

struct sl_wa_vframe {          /* the version-3 entry's second half       */
    short xx, xy, xz;          /* the axis the normal map's x scales     */
    short yx, yy, yz;          /* ... and its y; the normal is the z     */
};

/* The auxiliary entry `i` of an asset, and its frame: the entries are
 * SL_WA_AUX_SIZE apart in a version-2 file and SL_WA_AUX3_SIZE in a
 * version-3 one (`aux_stride`), the first twelve bytes the same either way.
 * The frame exists only when `vframe` is non-zero. */
#define SL_WA_VAUX(a, i) \
    ((const struct sl_wa_vaux *) (const void *) \
     ((const unsigned char *) (a)->vaux + (unsigned long) (i) * (a)->aux_stride))
#define SL_WA_VFRAME(a, i) \
    ((const struct sl_wa_vframe *) (const void *) \
     ((const unsigned char *) (a)->vaux + (unsigned long) (i) * (a)->aux_stride \
      + SL_WA_AUX_SIZE))

struct sl_wa_matx {            /* one material-extension record, host order */
    unsigned mode;             /* SL_WA_MATX_*; NONE draws the base alone */
    unsigned normal_tex, sphere_tex, offset_tex;   /* < texture_count      */
    unsigned flags;            /* SL_WA_MATX_F_*                          */
    float    base_weight[3];
    float    reflect_weight[3];
    float    warp_scale;
    float    normal_scale, normal_bias;
    float    sphere_scale[2], sphere_bias[2];
    float    mean_weight, alpha_floor;
};

struct sl_wa_anim {            /* one animation record, host order (#69) */
    unsigned mode;             /* SL_WA_ANIM_SINEWAVES                   */
    float    coord_scale;      /* what the raw pair is read at, > 0      */
    unsigned wave_count;       /* lanes in use, 1 .. SL_WA_ANIM_WAVES    */
    float    amplitude[SL_WA_ANIM_WAVES];
    float    phase[SL_WA_ANIM_WAVES];
    float    speed[SL_WA_ANIM_WAVES];
    float    freq_a[SL_WA_ANIM_WAVES];
    float    freq_b[SL_WA_ANIM_WAVES];
    float    turn_scale, turn_bias, turn;
    float    poly[4];
};

struct sl_wa_asset {
    unsigned asset_class;
    char     path[SL_WA_PATH_MAX];
    float    bounds_min[3];
    float    bounds_max[3];

    unsigned vertex_count;
    unsigned index_count;
    unsigned material_count;
    unsigned texture_count;
    unsigned mesh_count;
    unsigned anim_count;                     /* 0 on a bundle that moves nothing */
    unsigned aux_stride;                     /* SL_WA_AUX_SIZE or SL_WA_AUX3_SIZE */
    int      vframe;                         /* 1: SL_WA_VFRAME is valid (v3) */
    int      matx_count;                     /* materials with a record != NONE */

    const struct sl_wa_vertex *vertices;
    const unsigned            *indices;      /* host-order u32 */
    const struct sl_wa_vaux   *vaux;         /* NULL when no aux section */
    struct sl_wa_material      materials[SL_WA_MAX_MATERIALS];
    struct sl_wa_texture       textures[SL_WA_MAX_TEXTURES];
    struct sl_wa_mesh          meshes[SL_WA_MAX_MESHES];
    struct sl_wa_anim          anims[SL_WA_MAX_ANIMS];
    struct sl_wa_matx          matx[SL_WA_MAX_MATERIALS];  /* one per material */
    /* #70, version 4 only: the skin (one entry per vertex) and the joints. */
    const struct sl_wa_skin   *skin;         /* NULL below version 4 */
    unsigned                   joint_count;
    struct sl_wa_joint         joints[SL_WA_MAX_JOINTS];

    /* Filled by the renderer the first time it uploads this asset; the
     * provider only stores them so one asset owns its own GL names. */
    unsigned gl_tex[SL_WA_MAX_TEXTURES];
    int      uploaded;
    /* Filled by the renderer the first time it draws a view model or a prop
     * with a REFLECTION material: one item-space direction per vertex, from
     * the item's own triangles. Kept with the asset, which is never unloaded. */
    float   *vm_normals;
};

/* ---- the policy ---------------------------------------------------------- */

/* Is an external world-asset set selected at all? 0 = no: no root is
 * resolved, no directory is opened and sl_wa_lookup always answers NULL.
 *
 * RE-ASKED EVERY TIME, not latched once, because the selection is now a
 * setting the player moves mid-session (F5). Going dark does NOT unload: an
 * asset already parsed stays resident and keeps the GL names the renderer
 * gave it, so returning to the set that owns it re-uses the same cache slot
 * and the same upload rather than duplicating either. */
int sl_wa_active(void);

/* The set's name ("xbla"), or "" when inactive. */
const char *sl_wa_set(void);

/* How many times the selection has MOVED since the first ask. The same
 * idiom sl_texprov_generation offers; nothing keys on it today. */
unsigned sl_wa_generation(void);

/* The resolved root (for the summary line). Never NULL; "" when inactive. */
const char *sl_wa_root(void);

/* THE LOOKUP. NULL means "there is nothing here, draw the original picture":
 * the provider is inactive, no file exists for this (class, path), or a file
 * exists and failed validation. Resolved at most once per (class, path) per
 * run - a miss and an invalid outcome are both remembered - so an absent asset
 * costs one open for the whole run and a bad one costs one diagnostic.
 *
 * `path` is the canonical item path; it is compared case-sensitively against
 * the file's own embedded path, so a file cannot answer for an item it does
 * not name. */
const struct sl_wa_asset *sl_wa_lookup(unsigned asset_class, const char *path);

/* The census line: set, root, how it resolved, lookups / loaded / missing /
 * invalid, and the geometry actually resident. */
void sl_wa_report(FILE *f);

/* Counters, for the renderer's own census line and for the tests. */
unsigned sl_wa_lookups(void);
unsigned sl_wa_loaded(void);
unsigned sl_wa_missing(void);
unsigned sl_wa_invalid(void);

/* ---- the anchoring contract, exposed so it can be tested without GL ------ */

/* WHICH world projection the game has loaded. Not a preference and not a
 * guess: the renderer sets this from the ADDRESS the game named, in the same
 * statement that decides the projection is a world one, so the two cannot
 * disagree. It decides only how the world-to-clip matrix is BUILT - whether
 * the seam has to supply a view or the projection already carries one.
 *
 * It deliberately does NOT decide what space a vertex has to be in. The two
 * projections do consume different spaces - fr.c's absolute world, and
 * bondview2.c's world minus the player's current_model_pos, the floating
 * origin its rooms and view matrices are built against (bgroomtrans.c:216-218,
 * bondview2.c:11415-11417) - and an earlier revision tracked that difference
 * by hand and got it wrong. sl_wa_view_origin below removes the question:
 * the eye it recovers is already expressed in whatever space its matrix
 * consumes, so (centre - eye) is the right offset in that space without
 * anyone naming the space. */
#define SL_WA_PROJ_PLAIN   1   /* fr.c's: the seam supplies world-to-view */
#define SL_WA_PROJ_FOLDED  2   /* bondview2.c's: the view is already in it */

/* WHERE A SKYDOME BELONGS. It is a polar map about its own centre - measured
 * on the Dam item: its U is the azimuth about the centre of its own bounds to
 * within 0.0002 of a turn, and its V is the elevation over -15.5..+90 degrees.
 * A polar sky map is only correct seen FROM that centre, so the centre is
 * placed on the EYE and the dome carries no parallax. Drawn at its raw
 * coordinates instead, the viewer stands 42641 units off-centre at the Dam -
 * outside the dome's own mean radius - which skews the mapping and leaves the
 * sky visibly sliding while walking.
 *
 * The offset a vertex needs is therefore (centre - eye), and the whole of the
 * difficulty is which eye. See below.
 *
 * THE EYE, RECOVERED FROM THE TRANSFORM ITSELF.
 *
 * `m` is a world-to-clip matrix in row-vector order: some view V times some
 * perspective P. This returns the point V puts at the view origin - the eye -
 * expressed in whatever space `m` consumes, so the caller needs to know
 * neither P nor which space that is.
 *
 * WHY, rather than asking the game where the camera is. The two answers are
 * not the same answer. A camera read from the player struct is the camera
 * NOW; the matrix standing at the seam was built earlier and carries the
 * camera as it was then. Measured on the Dam: anchoring a skydome to the
 * struct's eye leaves a residual of about 40 world units against the folded
 * projection, drifting frame to frame with the camera's own motion - which is
 * a sky that slides while walking. Against the plain projection, where the
 * seam builds the view itself from that same struct, the residual is 0.001.
 * Taking the eye FROM the matrix cannot disagree with the matrix.
 *
 * HOW. With P of the usual shape (only the two axis scales, the depth pair and
 * a -1 in the w column), the rows of m carry the camera basis scaled:
 * (m0,m4,m8) is the right axis times sx, (m1,m5,m9) the up axis times sy, and
 * (m3,m7,m11) the negated back axis. The scales are those vectors' lengths
 * because the basis is orthonormal, and the three dot products of the eye with
 * the basis fall out of m12/sx, m13/sy and m15. That reconstructs the eye
 * exactly, and it is checked: a matrix whose recovered basis is not orthonormal
 * is refused rather than guessed at.
 *
 * Returns 1 and fills q[3]; 0 if `m` is not of that shape. */
int sl_wa_view_origin(const float *m, float *q);

/* #68 WHICH POINT OF A SKYDOME'S OWN SPACE BELONGS ON THE EYE.
 *
 * Its MODEL-SPACE ORIGIN - not the centre of its bounds. The remaster draws
 * its skydome item under a modelview whose linear part is the camera's
 * rotation times one uniform scale and whose TRANSLATION IS ZERO, so the
 * point (0,0,0) of the item's own space is where the camera stands, whatever
 * the item's extent. Measured on the runtime's own dome draw, not inferred:
 * the vertex constants of that draw carry a zero translation on every frame
 * sampled, and rasterising the item under those constants reproduces the
 * runtime's framebuffer at two camera pitches, where the bounds-centre
 * reading reproduces neither.
 *
 * A dome's bounds need not be centred on its origin - the first one measured
 * is not, by about twenty thousand units along one axis and seventeen
 * thousand vertically - so the two readings look at different parts of the
 * sky: the bounds-centre one put the zenith rows of the image overhead,
 * where the source puts a band well down the dome.
 *
 * `bounds_centre` nonzero answers with the bounds centre instead: the rule
 * this replaced, kept as a deliberate control so one binary can draw both.
 * A pure function of its arguments, so the self-test needs no GL. */
void sl_wa_sky_anchor(const float bmin[3], const float bmax[3],
                      int bounds_centre, float out[3]);

/* #68 D-023: IS THIS MATERIAL DRAWN. A source material always is; a
 * GENERATED one (SL_WA_MAT_GENERATED) only while the generated zenith clouds
 * are on (`generated_on`, the renderer's SL_SKY_ZENITH, default on). One
 * place, so the skydome pass and the self-test answer the same question. A
 * pure function of its arguments. */
int sl_wa_material_drawn(unsigned flags, int generated_on);

/* DOES THIS MATERIAL TAKE THE DECAL DEPTH PRIORITY on a replacement ITEM (a
 * prop, character or view model drawn by the item pass): exactly when it
 * carries SL_WA_MAT_DECAL and the decal pass is on (`decal_on`, the
 * renderer's SL_BG_DECAL, default on). Every other material - opaque,
 * blended or cutout - is drawn with no offset. A pure function of its
 * arguments, so the item pass and the self-test answer the same question. */
int sl_wa_decal_priority(unsigned flags, int decal_on);

/* #81 THE DECAL PASS'S DEPTH ADJUSTMENT, IN THE ITEM'S UNITS (the stand-in
 * for the source's `c_zadjust`, whose value the bundle does not carry - see
 * SL_WA_MAT_DECAL). The polygon offset alone resolves a decal authored
 * EXACTLY on its surface. A decal can also stand a little BEHIND the surface
 * it marks: on Bunker 1 the dark beams on the cavern's wooden wall (one
 * translucent decal mesh, 30 triangles) lie in the plane x = -5317 while the
 * wall's planks stand 0.2 .. 1.5 item units in front of it. A depth offset is
 * worth more world units the farther away the pixel is, so the beam won far
 * off and lost near the eye - its near part vanished as the player walked up
 * (playtest mark 7, "part of the wall unloads"). Measured over every
 * background decal triangle with a parallel surface overlapping it: Bunker 1
 * 10 with a surface in front (max 1.496), Facility 12 (max 0.020), Runway 4
 * (0.004), Surface 4 (0.009), the Dam 67 (median 0.045).
 *
 * So a decal vertex is drawn at the depth of the point SL_WA_DECAL_PULL item
 * units nearer along its own line of sight - its screen position unchanged,
 * its depth that of a point that much closer (never more than half the way
 * to the eye). A blended decal writes no depth, so this can only let it win
 * against a surface within that distance in front of it; it occludes
 * nothing. SL_BG_DECAL_PULL=0 draws the offset alone, as before.
 *
 * sl_wa_depth_affine: the clip z of a projection C (row vectors, as the
 * background pass uses) as an affine function of its clip w, z = a w + b -
 * true of any perspective projection, folded view or not; 0 when C's z and
 * w rows are not parallel (no such relation). sl_wa_decal_pull_z: the clip
 * z, under z = a w + b, of the point `pull` (clip w units) nearer along the
 * same line of sight, for a vertex at clip w; w <= 0 returns a w + b.
 *
 * ALONG THE PLANE'S NORMAL, NOT THE SIGHT LINE (the playtest of that
 * fix, marks 5 and 6). A fixed distance along the sight line covers a gap
 * measured across the wall only when the wall is seen face-on: at mark 5 the
 * line of sight meets the planks at 67 degrees, the beam lies 2.0 units
 * behind them ALONG that line (0.8 across), and a long stretch of the beam
 * further down the corridor, seen more obliquely still, was lost - the stub.
 * The gap is a property of the wall (0.2 .. 1.5 across it), so the decal is
 * drawn at the depth of its own plane moved SL_WA_DECAL_PULL item units
 * toward the eye: on each sight line the point a fraction P / h nearer,
 * h being the eye's distance from the decal triangle's plane - exact, as
 * clip w is linear along the line. The same bound holds at every angle: it
 * wins against a surface within P of its plane, in front of it, and nothing
 * else. sl_wa_decal_plane_ratio: P / h for the plane through p0 p1 p2 (item
 * coordinates, as `eye`), 0 for a degenerate triangle, an eye on the plane or
 * no pull - the caller then pulls the fixed distance.
 *
 * AND AFFINE IN CLIP SPACE, which is what the replay actually exposed. The
 * cavern beam is one quad 1000 item units long whose near end lies BEHIND
 * the eye at mark 5. The sight-line pull is a per-vertex function that is
 * not affine in w and gives nothing for w <= 0, so the clipper cut the quad
 * between a pulled far vertex and an unpulled near one and the interpolated
 * pull faded to zero toward the eye: a straight cut, beam in front of it,
 * planks behind (measured: the whole beam draws with the planks hidden,
 * SL_BG_HIDE=3; every other knob leaves the cut). The plane's depth is
 * z' = a w + b / (1 - P/h) - the sight-line pull of w P / h, written as a
 * straight line in w, so clipping and interpolation keep it exact.
 * sl_wa_decal_plane_z: that z for a vertex at clip w and ratio P / h (at
 * most 0.5, half the way to the eye, as before); a ratio of 0 is a w + b. */
#define SL_WA_DECAL_PULL 2.0f
int   sl_wa_depth_affine(const float *C, float *a, float *b);
float sl_wa_decal_pull_z(float a, float b, float w, float pull);
float sl_wa_decal_plane_ratio(const float *p0, const float *p1, const float *p2,
                              const float *eye, float p);
float sl_wa_decal_plane_z(float a, float b, float w, float ratio);

/* #85 ONLY A DECAL WITH SOMETHING TO PULL PAST IS PULLED. The pull lets a
 * decal win against a surface within P of its plane - measured in its
 * plane's normal, so on a sight line that grazes the plane it reaches a
 * long way: an eye 10 item units from the plane takes the plane to a fifth
 * nearer on every line. On Bunker 2 at the top of the stairs (playtest mark
 * 3, eye 389.8 item units up) the upper floor (mesh 69, a decal-pass draw,
 * y = 400) lies behind the walls on such lines, and pulled it drew across
 * them as a thin line at eye height (SL_BG_DECAL_PULL=0, SL_BG_HIDE=69 and
 * SL_BG_DECAL=0 each remove it). That floor has nothing within P in front
 * of it to win against: the pull exists for a decal standing a little
 * behind its surface (Bunker 1's beams, 0.2 .. 1.5 units behind the planks).
 *
 * So `need[t]` (t = index position / 3) is set to 1 for a triangle of a
 * decal-pass mesh that has a surface outside the decal pass that writes
 * depth, parallel to it (SL_WA_COPLANAR_COS), off its plane by more than the
 * coplanar tolerance (an exact tie is the offset's) and by at most
 * SL_WA_DECAL_PULL plus it, and overlapping it in its plane; every other
 * entry is 0. Only those triangles are pulled; the rest keep the decal
 * offset alone. Returns the number set; 0 also when an allocation fails.
 * Pure: no GL, testable. */
unsigned sl_wa_decal_needs_pull(const struct sl_wa_asset *a, unsigned char *need);

/* #85 ONLY A DECAL THAT LIES ON SOMETHING TAKES THE DECAL OFFSET. The decal
 * pass's offset (glPolygonOffset -1, -2, rm_apply's G_RM_ZMODE_DEC) carries a
 * SLOPE term: a plane seen edge-on changes depth steeply across its one row
 * of pixels, and the term takes it that much nearer. On Bunker 2 in a
 * corridor (playtest run 20261007-085655, mark 2: eye 389.8 item units up)
 * the next room's floor (mesh 69, a decal-pass draw, y = 400, ten units
 * above the eye) lies behind the corridor's far wall, and offset it drew
 * across that wall as a thin line at eye height. Measured with the offset's
 * two terms apart: the units alone (0, -2) leave no line, the slope alone
 * (-1, 0) draws it; SL_BG_DECAL_PULL=0 leaves it (the pull is not taken
 * there, sl_wa_decal_needs_pull), SL_BG_HIDE=69 and SL_BG_DECAL=0 remove
 * it. That floor lies on nothing: it is the surface, and the offset exists
 * for a decal drawn on another one.
 *
 * So `need[t]` (t = index position / 3) is set to 1 for a triangle of a
 * decal-pass mesh that lies on a triangle of another mesh, parallel to it
 * (SL_WA_COPLANAR_COS) and overlapping it in its plane: a surface outside
 * the decal pass that writes depth, off its plane by at most
 * SL_WA_DECAL_PULL plus the coplanar tolerance (the surface the pull and
 * the offset win against), or another decal-pass surface within the
 * coplanar tolerance (the pairs the order rule settles, whose earlier member
 * keeps the offset the later one yields to). Every other entry is 0, and
 * those triangles draw without the offset. Returns the number set; 0 also
 * when an allocation fails. Pure: no GL, testable. */
unsigned sl_wa_decal_needs_offset(const struct sl_wa_asset *a, unsigned char *need);

/* #89 A DECAL THAT YIELDS WHILE LYING ON A SURFACE OUTSIDE THE PASS KEEPS
 * THE OFFSET, and the decal it yields to draws one offset nearer. The pair
 * rule (#85, sl_wa_coplanar_yield) drops the decal offset from the later of
 * two decal-pass surfaces in one plane so it sits behind the earlier one;
 * that is exact where the later one lies on nothing else (Bunker 2's cell
 * floor, a translucent decal-pass draw). On Archives two stacked decals lie
 * on an opaque floor outside the pass: playtest run 20261008-182214, mark 1
 * (a blood pool, meshes 60 and 61 of background/archives, both decal-pass
 * cutouts 0.0000 item units above floor mesh 1) and mark 2 (a ceiling fan's
 * shadow, meshes 86 and 88, the same arrangement). With the offset dropped
 * the later decal sits AT the floor's depth and the two resolve per pixel by
 * rasterisation noise: the overlapping part of mesh 61 and of mesh 88 (the
 * shadow's ring) appeared and vanished frame to frame over 13 consecutive
 * frames; SL_BG_HIDE=61 and SL_BG_HIDE=88 each leave the rest steady, and
 * SL_BG_HIDE=60 shows mesh 61's yielding part striped across the floor.
 *
 * So, given `yield` (the order rule's per-triangle answer), `layer[t]` (t =
 * index position / 3) is set to SL_WA_DECAL_KEEP for a yielding decal-pass
 * triangle that lies on a surface outside the pass in
 * sl_wa_decal_needs_offset's sense (another mesh, writing depth, parallel,
 * overlapping, within SL_WA_DECAL_PULL plus the coplanar tolerance) and
 * that does not repeat an earlier decal-pass triangle corner for corner -
 * such a repeat is a two-layer draw's layer over its own base, one draw of
 * the source (Surface's fence shadow, meshes 64 and 65), left as #85 draws
 * it - and
 * SL_WA_DECAL_OVER is or-ed into a decal-pass triangle of an EARLIER mesh
 * that such a triangle overlaps within the coplanar tolerance, that writes
 * depth (a translucent one holds no pixel and is never the member yielded
 * to, as in the order rule) and that does not yield itself. The renderer draws a KEEP triangle with the decal offset
 * under LESS (behind the earlier decal, in front of the surface), and an
 * OVER triangle with twice the decal offset - the same separation again,
 * between the two decals. A chain of three stacked decals on one surface is
 * not separated further: the third and the second keep the same offset.
 * Returns the number of KEEP triangles; 0 also when an allocation fails.
 * Pure: no GL, testable. */
#define SL_WA_DECAL_KEEP 1u
#define SL_WA_DECAL_OVER 2u
unsigned sl_wa_decal_layers(const struct sl_wa_asset *a, const unsigned char *yield,
                            unsigned char *layer);

/* DOES THIS MATERIAL SAMPLE ITS COVERAGE FROM THE IMAGE'S BLUE CHANNEL:
 * exactly when it carries SL_WA_MAT_COVERAGE_BLUE, is ALPHA-blended, and the
 * rule is on (`coverage_on`, the renderer's SL_WA_COVERAGE, default on; off
 * draws the image's own alpha, the earlier result, as a control). Every
 * other material keeps its image's alpha. A pure function of its arguments,
 * so the item pass and the self-test answer the same question. */
int sl_wa_coverage_blue(unsigned flags, unsigned blend, int coverage_on);

/* #94 WHICH TEXTURE ENVIRONMENTS THE LINEAR SHADE PROGRAM TAKES (sl_gfx_dl.c,
 * SHADE_LINEAR_FS, and with it the per-object tint u_obj before the row's
 * fog): 0 (modulate), 3 (alpha add) and 4 (alpha lerp) always; 2 (alpha the
 * vertex's alone, AEQ_REPLACE - an object the shots have darkened draws its
 * lists so) only while the remaster's stage row is in force (`row`) and the
 * rule is on (`on`, SL_OBJ_REPLACE_PROG), so that a shot object's register
 * tint is applied there and not dropped on the fixed pipeline (#77 gives that
 * pipeline's one fog stage to the row). Every other environment stays on
 * the fixed pipeline. Without a row the answer is the earlier one, so
 * ORIGINAL and COMMUNITY HD are untouched. Pure. */
int sl_wa_lin_takes(int texenv, int row, int on);

/* #94 A CUTOUT IS LAID AFTER THE SOLID WORLD BEHIND IT.
 *
 * A cutout writes depth AND blends its surviving soft edge (SL_WA_BLEND_BLENDS):
 * an edge fragment at alpha a leaves a times its own colour plus 1 - a times
 * whatever the frame holds under it at that moment, and its depth then hides
 * everything behind it drawn later. The background pass draws meshes in the
 * source's command-list order, and in that order a cutout can come before
 * the opaque surfaces it stands in front of: Jungle's canopy (mesh 41, 18288
 * triangles) precedes the opaque meshes 42 .. 53. Each leaf edge was blended
 * over the aperture's black clear (sl_wa_backdrop_black) and then kept the
 * far jungle out, so under the stage fog every leaf of the far canopy drew
 * as a black outline (Jungle playtest mark 1, 2026-10-10: pixels under half
 * the frame's median luminance 4993 -> 328 over the upper frame with the
 * cutouts laid last; SL_BG_FOG=0 shows the outlines over the lit jungle too).
 *
 * The first draw therefore lays the item in two runs of the source's order:
 * first every mesh but the deferred ones, then the deferred ones. A mesh is
 * DEFERRED when it lies at or after the item's first cutout mesh and is
 * either a cutout or a decal (SL_WA_MAT_DECAL): a decal keeps its place
 * after the cutouts it was authored after, since one may lie on a cutout,
 * and every decal still follows the surface it was authored on. Meshes
 * before the first cutout keep their place, so an item with no cutout, or
 * with nothing but cutouts and decals after its first cutout, draws exactly
 * as before. Opaque and panorama surfaces after the first cutout move ahead
 * of it - they write depth and blend nothing, so only what the cutout's edge
 * blends over changes. Translucent meshes of the game's translucent walk
 * (#90) are drawn there, after every cutout, as before.
 *
 * sl_wa_first_cutout gives the index of the item's first cutout mesh (the
 * mesh count when it has none); sl_wa_mesh_deferred answers for mesh `mi`
 * given that index (0 for an index out of range). Pure, no GL. The pass
 * draws the second run only while SL_BG_CUTOUT_LAST is unset or nonzero;
 * SL_BG_CUTOUT_LAST=0 draws the source's order (the A/B). */
unsigned sl_wa_first_cutout(const struct sl_wa_asset *a);
int sl_wa_mesh_deferred(const struct sl_wa_asset *a, unsigned mi, unsigned first_cut);

/* #90 THE TRANSLUCENT HALF OF THE BACKGROUND IS DRAWN IN THE GAME'S
 * TRANSLUCENT WALK.
 *
 * The game draws a frame's rooms in two walks (bg.c sub_GAME_7F0B3C8C): per
 * room the solid objects and the room's PRIMARY list, then, after every room,
 * each room's SECONDARY (translucent) list followed by its translucent
 * objects. So the game's glass is laid over every character and solid prop
 * already on the picture, and a guard behind a window shows through it.
 *
 * The replacement background is drawn whole at the frame's first world draw
 * (the seam in sl_gfx_dl.c), so an ALPHA-blended draw of the item - which
 * writes no depth - was laid BEFORE the characters, and every one drawn later
 * painted over it as if in front. Measured on Streets (playtest run
 * 20261008-235001-lvl29, mark 1): the window of room 12 is mesh 60, one
 * ALPHA draw carrying the frame and the glass in one image, in the facade's
 * plane (z 5672.7 game units); the guard standing at it is at z 5865, 193
 * units inside the room, and drew in front of the mullions and the glass
 * while the opaque wall beside the window cut him - the guard and the window
 * traded places as the view moved. Hiding mesh 60 leaves the room and the
 * guard; under ORIGINAL the bars cover him.
 *
 * The rule: a mesh whose material is ALPHA-blended is drawn LATE - in the
 * game's own range, after the frame's solid world, when the walk reaches the
 * first room SECONDARY list (or, on a frame without one, at the first-person
 * weapon, the first 2D draw or the end of the walk, whichever comes first).
 * Its depth, fog and offsets are the ones computed for the frame's opaque
 * draw, unchanged. Opaque and CUTOUT draws (which write depth) stay where
 * they were; the panorama shell (SL_WA_MAT_PANORAMA) stays in the first draw,
 * as does every draw beyond the game's far plane (the first depth range,
 * where nothing of the game's can lie). A late draw changes a pixel only
 * where something drawn between the two lies behind it - the order is the
 * only thing that moves. `on` is the renderer's SL_BG_XLU_LATE (default on;
 * 0 draws the whole item at the first world draw, as before).
 *
 * A DECAL ON A SOLID SURFACE IS DRAWN WITH IT. A decal-pass mesh lying on a
 * surface that writes depth takes that surface's depth (the decal offset and
 * pull), and the surface is drawn in the first draw; so is such a decal -
 * before any translucent surface in front of it, which is then laid over it.
 * Drawn late with the rest, decals and translucent surfaces were laid in the
 * item's own order, neither writing depth, so a decal listed after a window
 * painted over the window wherever it lay behind it. Measured on Streets
 * (playtest run 20261009-004715-lvl29, mark 1): the torn-paper decals and
 * the graffiti of room 12 (meshes 90, 92, 94; DECAL, ALPHA, on its opaque
 * walls, the back wall at z 6196) are listed after the window (mesh 60,
 * z 5672.7) and drew over its mullion and jamb, about 520 units in front,
 * steady over 13 frames and through a strafe; SL_BG_HIDE=90,92,94 removes
 * them and nothing inside the guard's outline changes.
 *
 * Two kinds stay late, in the item's order. A decal with a translucent
 * surface lying on it or just over it is part of one layered construction
 * and keeps the order the item lists the layers in: Streets' mesh 61 lies on
 * that window's glass (a tie), pieces of 91 and 93 straddle a window's edge, and Dam's
 * bridge planks (mesh 85, decal-pass, on an opaque deck) lie 3.1 item units
 * under the translucent ramps (mesh 59, the same image) listed before them -
 * drawn first, the ramps were laid over the planks, 262 pixels of the
 * stage's start frame. And a decal lying on nothing that writes depth (#85's
 * lone decal) has no depth beneath it in the first draw, so a translucent
 * surface behind it would be laid over it. `late_decal` is the answer for
 * the triangles being drawn (sl_wa_decal_late, non-zero: late); a material
 * outside the decal pass ignores it. Pure. */
int sl_wa_blend_late(unsigned flags, unsigned blend, int late_decal, int on);

/* #90 WHICH TRANSLUCENT DECAL PIECES ARE DRAWN LATE (the question
 * sl_wa_blend_late's `late_decal` asks), piece by piece: a decal-pass mesh is
 * a scatter of pieces over the whole stage, and the answer differs between
 * them. Streets' mesh 91 (playtest run 20261009-073643-lvl29, mark 1) has a
 * piece across a facade window's edge (on the window's glass, a tie) and
 * another on room 12's side wall at x 5997, behind that window; asked of the
 * mesh, the first piece took the second late with it, and it drew over the
 * window's jamb under the mark's crosshair.
 *
 * A triangle t of an ALPHA decal-pass mesh, not the panorama shell, is under
 * a translucent surface when a triangle of another mesh that is ALPHA,
 * outside the decal pass and not the panorama shell is parallel to it
 * (SL_WA_COPLANAR_COS), off its plane by at most SL_WA_DECAL_XLU_NEAR plus
 * the coplanar tolerance (either side, a tie included) and overlaps it in its
 * plane; it is lone when it lies on no triangle of another mesh that writes
 * depth (opaque or cutout, in the pass or not), "lies on" being
 * sl_wa_decal_needs_offset's distance (parallel, off the plane by at most
 * SL_WA_DECAL_PULL plus the tolerance, overlapping). A PIECE is the triangles
 * of one mesh joined through shared corners - corner positions within the
 * coplanar tolerance of each other, as the source writes a piece's faces
 * with vertices of their own that differ in the last bits. `late[t]` (t = index
 * position / 3, the array a->index_count / 3 long) gets SL_WA_DECAL_LATE_XLU
 * for every triangle of a piece with a triangle under a translucent surface
 * (Dam's planks: 12 of mesh 85's 28 triangles are not themselves under the
 * ramps, and per triangle they went first - 12 pixels of the start frame),
 * and SL_WA_DECAL_LATE_LONE for every triangle of a mesh with a lone
 * triangle (per piece, Bunker 2's start frame moved 6037 pixels; a mesh with
 * a lone piece stays late whole, as before). Every other entry is 0. Returns
 * the number of triangles marked; 0 also when an allocation fails (every
 * translucent decal is then drawn in the first draw). Pure: no GL, testable.
 *
 * sl_wa_decal_mix: of mesh `mesh`, SL_WA_DECAL_MIX_LATE when a triangle is
 * marked in `late`, or-ed with SL_WA_DECAL_MIX_EARLY when one is not - the
 * draws that mesh takes part in. A mesh whose index range is not whole
 * triangles is LATE whole; with no array (`late` NULL) every mesh is EARLY.
 * Pure.
 *
 * SL_WA_DECAL_XLU_NEAR is a bound between two measurements, not a fitted
 * value: Dam's ramps lie 3.1 item units over the planks they belong with,
 * and Streets' window about 422 in front of the wall decals it must cover
 * (z 4577 against 4999). A translucent surface between 8 and 422 units off
 * a decal on a solid surface is untested: it is laid over the decal. */
#define SL_WA_DECAL_LATE_XLU  1u
#define SL_WA_DECAL_LATE_LONE 2u
#define SL_WA_DECAL_XLU_NEAR  8.0f
#define SL_WA_DECAL_MIX_LATE  1u
#define SL_WA_DECAL_MIX_EARLY 2u
unsigned sl_wa_decal_late(const struct sl_wa_asset *a, unsigned char *late);
unsigned sl_wa_decal_mix(const struct sl_wa_asset *a, const unsigned char *late, unsigned mesh);

/* ---- D-026 THE PANORAMA SHELL FADES WHERE THE FOG COVERS IT --------------
 *
 * A fully fogged panorama shell contributes the fog colour and nothing else,
 * and it is opaque, so the sky dome drawn behind it is hidden down to the
 * mountains. Under D-026 that contribution fades toward transparency with
 * elevation, so the dome shows above a band of haze:
 *
 *   - HAZE. Below a height the shell stays exactly as drawn. That height is
 *     taken, per shell vertex and per frame, from the scenery itself: the
 *     highest point, seen from the eye, of any other background vertex the
 *     stage fog covers (sl_wa_panorama_reach) within an azimuth window round
 *     the vertex (sl_wa_panorama_taper). Scenery the fog has flattened to its
 *     colour therefore always stands against fog-coloured shell, never against
 *     the sky - a flat silhouette with a hard top is exactly what a fade
 *     without this rule would expose.
 *   - FADE. Above it the shell's coverage falls smoothly to zero over
 *     SL_WA_PANO_FADE degrees of elevation (sl_wa_panorama_coord,
 *     sl_wa_panorama_alpha), so there is no line where it ends.
 *   - FULLY FOGGED ONLY. A shell vertex the fog does not cover completely is
 *     drawn exactly as before (sl_wa_panorama_fogged): partly fogged scenery
 *     keeps the ordinary fog equation, and a fog range long enough to reach
 *     the ring (the Dam's intro row) changes nothing. The rule ENTERS
 *     smoothly, over the last SL_WA_PANO_COVER of the fog coordinate: a
 *     shell vertex's fog coordinate moves with the eye, and one face of the
 *     shell is hundreds of degrees of sky wide, so a step at exactly 1 (the
 *     first form) switched a whole face between drawn and faded in one frame
 *     as a vertex crossed it - the distant "pop" marked in play walking on
 *     Surface (#80, run 20261005-120919, marks 1 - 4; measured as 87 -> 85
 *     fully fogged shell vertices between two frames of a walk).
 *
 * Colour, fog start, fog end, fog colour and the distance equation are the
 * stage fog's own; only the shell's coverage moves. SL_SKY_PANORAMA=0 draws
 * the shell as authored. Pure functions, so the pass and the self-test answer
 * the same questions. */

#define SL_WA_PANO_REACH  0.85f  /* a fog coordinate from which scenery raises the haze */
#define SL_WA_PANO_FADE   6.0f   /* degrees of elevation the coverage fades over */
#define SL_WA_PANO_COVER  0.95f  /* a shell vertex's fog coordinate from which the fade enters */

/* Does this mesh's material take the D-026 rule: it carries
 * SL_WA_MAT_PANORAMA, it consumes the stage fog (SL_WA_MAT_FOG), and the rule
 * is on (`on`, the renderer's SL_SKY_PANORAMA, default on). */
int sl_wa_panorama_fades(unsigned flags, int on);

/* ---- D-023 THE CLOUD FIELD: the game's cloud plane and its phase ---------
 *
 * sky.c casts every sky ray from the eye onto a horizontal plane `height` world
 * units up (the environment's CloudRepeat) and textures it in the RDP's 10.5
 * fixed-point units as s = 0.1 * x, t = 0.1 * z + phase, where phase is
 * g_SkyCloudOffset - advanced by skyTick one g_ClockTimer step per game tick
 * (none while lv.c holds the timer at zero) and wrapped at 4096. One repeat of its w x h cloud image
 * is 32 * w by 32 * h of those units.
 *
 * sl_wa_cloud_coord applies exactly that, for a direction `d` from the eye, and
 * normalises by the generated tile, which stands for 1 / SL_WA_CLOUD_TILES of
 * one image repeat: out[0..2] = (u*q, v*q, q), the projective coordinate GL
 * interpolates exactly across a triangle of directions. The eye's own position
 * and the phase are reduced modulo one tile first (exact: whole tiles never
 * show), which keeps the coordinates small for any eye and any session length;
 * because 4096 is a whole number of tiles for every image the game uses
 * (32 * 64 / 2 = 1024), the game's wrap of the phase is invisible here as it
 * is there. Returns 0 - and q = 0 - for a direction at or below the horizon or
 * an eye at or above the plane, which the game never clouds either. A pure
 * function, so the pass and the self-test agree.
 *
 * SL_WA_CLOUD_TILES is a pixel DENSITY, not a speed: the field moves exactly as
 * the game's plane does whatever it is. The tile is grown from the dome's band
 * at the band image's own density (17.15 pixels a degree on the Dam), and two
 * tiles a repeat put one tile pixel at one band pixel where the band itself is
 * seen - its settled ring about 30 degrees up from the walkway, the cloud plane
 * about 4770 units above the eye: 1/17.15 degree there is 9.7 world units, a
 * tile pixel 32 * 64 / 2 / 0.1 / 1024 = 10.0. One tile a repeat measured half
 * as sharp and flatter than the zenith it replaces. */
#define SL_WA_CLOUD_TILES 2u

int sl_wa_cloud_coord(const float d[3], const float eye[3], float height,
                      float phase, unsigned img_w, unsigned img_h, float out[3]);

/* 1 where a shell vertex's fog coordinate says the fog covers it completely,
 * 0 up to SL_WA_PANO_COVER, smooth between - so the fade enters continuously
 * as the eye moves and never switches a face of the shell in one frame. */
float sl_wa_panorama_fogged(float fogc);

/* How strongly a scenery vertex with fog coordinate `fogc` raises the haze:
 * 0 up to SL_WA_PANO_REACH, 1 at full fog, smooth between - so the haze moves
 * continuously as scenery drifts into the fog. */
float sl_wa_panorama_reach(float fogc);

/* The azimuth window: weight 1 for scenery within `inner` degrees of a shell
 * vertex, falling smoothly to 0 at `outer`. */
float sl_wa_panorama_taper(float dphi, float inner, float outer);

/* Where a point at elevation tangent `tan_e` sits in the fade whose haze ends
 * at elevation tangent `tan_top` (>= 0): 0 at the haze top, 1 SL_WA_PANO_FADE
 * degrees above it (or `fade` degrees when positive), linear in the tangent -
 * which is linear up the shell's own vertical faces. */
float sl_wa_panorama_coord(float tan_e, float tan_top, float fade);

/* The shell's coverage at fade coordinate `s`: 1 at and below 0, 0 at and
 * above 1, a smoothstep between. */
float sl_wa_panorama_alpha(float s);

/* #75 THE ORIGINAL'S STATE, READ BY THE REPLACEMENT.
 *
 * THE NEAR-CUT CONTRACT OF REPLACEMENT GEOMETRY. A replacement surface the
 * camera reaches stays closed: whatever part of a replacement triangle lies in
 * front of the eye but nearer than znear is drawn at znear instead of being
 * cut away. The original renderer does this for an EYE-CROSSING triangle
 * (B-100, emit_tri) and cuts a triangle wholly in front of the eye at the near
 * plane, as the cartridge does, because both are the cartridge's own
 * triangles. A replacement item's triangulation is its own - the same camera
 * finds one of its walls eye-crossing a few units away and wholly in front
 * but inside znear a few units further on - so the cartridge's per-triangle
 * cut has no counterpart there, and the replacement keeps both populations.
 *
 * Per vertex, `cls[v]` holds SL_WA_NEAR_BEHIND (clip w at or behind the eye)
 * and SL_WA_NEAR_INSIDE (in front of the eye but nearer than znear: z < -w
 * with w > 0). sl_wa_near_cut_select copies into `out` the index triples of
 * the triangles among `idx[0..n)` that GL's near plane cuts while some part
 * stays in front of the eye - a BEHIND or INSIDE vertex, and a vertex that is
 * not BEHIND - and returns how many indices it wrote (a multiple of 3). `out`
 * may be NULL to count only. SL_WA_NEAR_EYE_ONLY in `mode` restricts it to
 * the eye-crossing population (the #75 first form, kept for the A/B). */
#define SL_WA_NEAR_BEHIND   1u
#define SL_WA_NEAR_INSIDE   2u
#define SL_WA_NEAR_EYE_ONLY 1u
unsigned sl_wa_near_cut_select(const unsigned char *cls, const unsigned *idx,
                               unsigned n, unsigned mode, unsigned *out);

/* The class bits of one clip-space position (x, y, z, w). */
unsigned sl_wa_near_class(const float *c);

/* #77 TWO OF THE ITEM'S OWN SURFACES IN ONE PLACE: which one shows.
 *
 * The remaster draws its background with the depth test LESS (the console's
 * RB_DEPTHCONTROL on 317 of the Dam's 323 opaque draws) and in its own list
 * order, so where two of the item's surfaces lie exactly in one plane the
 * one drawn FIRST keeps the pixels: a later surface at an equal depth fails
 * LESS. This renderer tests LEQUAL (B-122), under which the later one wins.
 * On Facility the hazard stripe beside the lab windows (drawn before the
 * floor, in the floor's plane, not in the decal pass) was covered by the
 * floor that way.
 *
 * Testing the whole pass LESS is not the answer: everywhere else it moves
 * pixels where two surfaces merely meet (edge ties; measured on the Dam's
 * witness frames). So the order is restored only where it decides anything:
 * `yield[t]` (t = index position / 3, over the whole index list) is set to 1
 * for a triangle of a depth-writing mesh that lies in the plane of a triangle
 * of an EARLIER depth-writing mesh of the same pass, decal or not (normals within
 * SL_WA_COPLANAR_COS, plane offsets within sl_wa_coplanar_eps of each
 * other) and overlaps it with positive area (separated by no axis of either
 * triangle by more than minus that tolerance, in the plane's dominant
 * projection); every
 * other entry is 0. Such a triangle is drawn with LESS and yields to the
 * earlier surface; nothing else changes. A decal and a surface outside the
 * decal pass never pair: the decal pass's own offset and pull order them. Two
 * decal-pass surfaces do (#85): both carry that offset and pull, so between
 * them nothing but the order decides - at Bunker 2's cells the floor (mesh
 * 69) is itself a decal-pass draw listed after the stain on it (mesh 61),
 * 0.0000 units apart, and covered it in some frames and not others. Such a
 * pair is never two sides of a wall, and the renderer draws a yielding decal
 * triangle without the decal offset (sl_gfx_dl.c). A translucent draw writes
 * no depth and only yields (#81, sl_wa_coplanar_opposed_ex below). A mesh whose index
 * range does not start on a triangle is left out. Returns the number of
 * yielding triangles; 0 also when an allocation fails (the previous
 * drawing). Pure: no GL, testable. */
#define SL_WA_COPLANAR_COS 0.9999
#define SL_WA_COPLANAR_EPS 0.01
/* THE TOLERANCE GROWS WITH THE PLANE'S DISTANCE FROM THE ORIGIN. A position
 * is a float32, so a coordinate of magnitude 41000 is held to 2^-8 of a unit;
 * a triangle's normal built from three such positions over an edge of a few
 * hundred units tilts by about 1e-5, and its plane offset (normal . vertex,
 * taken at that distance) wanders by |offset| x 1e-5 - 0.4 units at 41000,
 * forty times the flat tolerance above. Measured on Surface's dome
 * (background meshes 12 and 16, the inside and outside of one shell on
 * vertices they share, at coordinates up to 41166): 96 overlapping pairs
 * whose normals agree to 1.000000 have offsets 0.0005 .. 0.4021 apart, so
 * every pair the flat tolerance missed fought for the cap, pixel by pixel.
 * The tolerance is therefore the flat one plus SL_WA_COPLANAR_REL times the
 * larger coordinate magnitude of the pair (the farthest any of their
 * vertices lies from the origin on an axis - the size whose rounding the
 * normal carries, whatever the plane's orientation): 0.01 near the origin,
 * 0.83 at the dome. A pair this admits that is not in one plane is not
 * harmed by it: the yield changes only an exact depth tie, and the push of
 * a side facing away is by this same tolerance (sl_gfx_dl.c, the two-sided
 * draw). */
#define SL_WA_COPLANAR_REL 2e-5
double sl_wa_coplanar_eps(double mag1, double mag2);
unsigned sl_wa_coplanar_yield(const struct sl_wa_asset *a, unsigned char *yield);

/* #77 TWO SIDES OF ONE WALL. Some of those pairs are not two surfaces on one
 * side: they face opposite ways - a wall of one room authored back to back
 * with a wall of the next, the two planes within a few ten-thousandths of a
 * unit (Facility's bathroom wall and the stairwell behind it). The item is
 * drawn with culling off, as the remaster draws it (the console's
 * PA_SU_SC_MODE_CNTL 0x4 on every background draw at the Dam), so each side
 * also draws the other wall's back face in its own plane; the order rule
 * above then hands the pixels to whichever was listed first - the other
 * room's wall seen from behind, or a depth tie striped across both.
 *
 * Of two such surfaces the one facing the eye is the one seen. Same walk and
 * same thresholds as sl_wa_coplanar_yield, which it also fills (`yield`
 * exactly as that function would); `opp[t]` is set to 1 for every triangle
 * in a pair whose AUTHORED normals (cross(B - A, C - A)) point opposite
 * ways, either member. Returns the number of such triangles. Pure: no GL.
 *
 * A pair inside ONE mesh counts too (#80): the order rule skips it, since a
 * mesh is one draw in the item's own order, but a plate authored with its
 * top and its underside in one mesh is two sides of one surface all the
 * same. Surface's sniper tower platform is one (mesh 5, top and underside
 * triangulated differently, offsets 0.03 apart): from above the underside
 * took half the plate, a different half from one frame to the next. Such a
 * pair is marked opposed and yields nothing. */
unsigned sl_wa_coplanar_opposed(const struct sl_wa_asset *a, unsigned char *yield,
                                unsigned char *opp);

/* #81 A TRANSLUCENT SURFACE ON AN EARLIER ONE YIELDS TOO. The order rule
 * above is the remaster's LESS, and its translucent draws are tested the
 * same way: a translucent triangle lying exactly on an earlier surface of
 * its own pass (#85: decal or not)
 * that writes depth fails it and draws nothing. On Bunker 1 the control
 * room's light pillars carry a translucent glow sheet (mesh 57, a white blob
 * on black whose image and vertex alpha are opaque everywhere) in the
 * pillar's own plane; the remaster shows the pillar and its lamp bar with no
 * sheet (private emulator, the playtest mark 5 view), this renderer drew it
 * LEQUAL as black squares with blobs (mark 3). Such a triangle is marked in
 * `yield` like an opaque one; it never holds a pixel against a later one and
 * is never a side of a wall. The two functions above include it;
 * `translucent` 0 leaves translucent meshes out, the rule before #81
 * (SL_BG_XLU_YIELD=0 in the renderer). */
unsigned sl_wa_coplanar_opposed_ex(const struct sl_wa_asset *a, unsigned char *yield,
                                   unsigned char *opp, int translucent);

/* Whether a triangle's authored normal, cross(B - A, C - A), points toward
 * `eye` (item coordinates). An edge-on triangle answers 0. Pure. */
int sl_wa_tri_faces(const struct sl_wa_vertex *A, const struct sl_wa_vertex *B,
                    const struct sl_wa_vertex *C, const float *eye);

/* #96 THE FACE CULL: whether the background pass withholds a triangle of a
 * material with `mat_flags` this frame - SL_WA_MAT_CULL_BACK set, the rule
 * `on`, the frame's eye in hand (`eye_ok`), and the triangle not facing it
 * (`faces`, sl_wa_tri_faces). With no eye nothing is withheld. Pure. */
int sl_wa_cull_drops(unsigned mat_flags, int on, int eye_ok, int faces);

/* D-048 THE GAME'S WATER: what an SL_WA_MAT_GAME_WATER material is drawn
 * under this frame, from the water controller's three phases
 * (sub_GAME_7F092E50, unk_092E50.c): `ps` and `pt` tile 0's origin in
 * quarter-texels ([0, 256), the S and T phases), `pw` the blend phase
 * (radians). The game's setup list draws the image through two wrapping
 * 64-texel tiles - tile 0 at (ps, pt), tile 1 at the controller's
 * ((int) p + 90) & 0xFF and ((int) p + 150) & 0xFF with p's fraction kept,
 * as B-109 keeps it - and lerps tile 0 toward tile 1 by PRIM_LOD_FRAC, the
 * byte (u32) (sinf(pw) * 127 + 128). The RDP samples s - uls, so a tile
 * whose origin stands at s_q quarter-texels shows the image moved by -s_q / 4
 * texels; in the item's coordinates (u = s / 2048 + c, v = c' - t / 2048, one
 * unit per 64 texels) that is u - s_q / 256 and v + t_q / 256. Out: each
 * tile's shift of (u, v) and the lerp fraction (the byte / 255). Pure. */
struct sl_wa_water {
    float du0, dv0;     /* tile 0: add to the stored (u, v) */
    float du1, dv1;     /* tile 1 */
    float frac;         /* tile 1's weight, [0, 1] */
};
void sl_wa_water_state(float ps, float pt, float pw, struct sl_wa_water *w);

/* sl_wa_light_mark - which vertices of an item show a shot-out light.
 * `pts[0..npts)` are the game's darkened fixture vertices, in the item's own
 * coordinates; `reach` is the game's fixture reach (lightfixture.c: a sum of
 * the three absolute axis differences below it). A triangle of `idx[0..ni)`
 * whose three vertices all lie within the reach of some point is the
 * fixture's; each of its vertices gets `mark[v] = 1` (every other vertex 0).
 * Returns the number of such triangles. */
unsigned sl_wa_light_mark(const struct sl_wa_vertex *v, unsigned nv, const unsigned *idx,
                          unsigned ni, const float (*pts)[3], unsigned npts, float reach,
                          unsigned char *mark);

/* sl_wa_fold_eye - the eye in the space the VIEW-FOLDED world projection's
 * modelview output is in: (camera position - render origin) x visibility.
 * `wv` is a world-to-view matrix (column-major, sl_wa_view_matrix's), `ro`
 * the render origin, `vis` the visibility scale. */
void sl_wa_fold_eye(const float *wv, const float *ro, float vis, float *out);

/* sl_wa_redraw_quad_ok - is (p, q, r, s) ONE quad seen through two
 * triangulations? The authored-redraw path (B-125/B-126, sl_gfx_dl.c) takes a
 * redraw triangle (p, q, r) whose edge p-r was never drawn, and a vertex s
 * that both drawn edges p-q and q-r had opposite them, as a quad re-cut along
 * the other diagonal. That holds only when the diagonals p-r and q-s actually
 * CROSS: their closest approach lies inside both segments and the gap there
 * is small beside them. A box corner satisfies the edge test too - faces
 * meeting at q - and there the two "diagonals" are skew lines whose closest
 * point sits at an end. Positions in any one Euclidean frame. Returns 1 for a
 * real quad (planar or gently bent), 0 otherwise. */
#define SL_WA_REDRAW_QUAD_END 0.01f   /* closest approach this far inside each diagonal */
#define SL_WA_REDRAW_QUAD_GAP 0.25f   /* gap at most this fraction of the shorter diagonal */
int sl_wa_redraw_quad_ok(const float *p, const float *q, const float *r, const float *s);

/* WHAT RECTANGLE THE ASSET IS ALLOWED TO PAINT, and why it is not the one it
 * finds.
 *
 * The seam draws on the frame's first world triangle, which is inside the
 * first ROOM's display list - and GoldenEye issues `ED setscissor` before
 * every room's list with that room's PORTAL WINDOW. So the scissor standing
 * at the seam is one room's aperture, the portal traversal decides which room
 * is first and how wide its window is, and both change with the camera's
 * HEADING. A background cut to that is a background that changes when the
 * player turns on the spot.
 *
 * Measured at the Dam's first alarm button, teleport-sampled, one pose per
 * run: the box found at the seam was [654,30 303x660] at heading 246.3,
 * [516,30 441x660] at 207.0 and [3,30 954x660] - the whole picture - at
 * 192.6, against a viewport of [0,30 960x660]; and the asset's own pixels
 * fell inside that box 98.1%, 100.0% and 100.0% of the time. The playtest's own
 * frames carry the matching artefact: one hard vertical edge at the box's
 * left side in the first two headings and none in the third.
 *
 * A background covers the whole picture, and the rectangle clip space maps
 * onto is the VIEWPORT - so that is the aperture, whatever was found. This is
 * a function rather than four lines at the seam so the rule can be tested
 * without a GL context: what must hold is that the answer does not depend on
 * `found` at all. */
void sl_wa_sky_aperture(const int viewport[4], const int found[4], int box[4]);

/* #68 THE FOG CONTRACT AN EXTERNAL WORLD BACKGROUND RECEIVES.
 *
 * A replacement background stands in for ROOM BACKGROUND geometry, and the
 * game draws that under fog it establishes itself: `fogSetRenderFogColor`
 * (bgfog.c:620) writes SetFogColor with the environment's RGB, then
 * `gSPFogPosition` with the environment's band, then G_FOG, and only then the
 * room's list. The replacement has to receive the same thing, and the point
 * of this function is WHERE that is read from.
 *
 * NOT from the state standing at the seam. The seam draws on the frame's
 * first world triangle and that is not always a room triangle: bg.c:737 draws
 * a room's PROPS before bg.c:755 draws its background, and a prop carries its
 * own fog register and its own blender mux. Measured on the Dam at the gate
 * yard, classifying the replacement from the mux standing at the seam gave
 * the room's A_SHADE on most frames and a prop's A_FOG - whose colour alpha
 * is zero, i.e. no fog at all - on others, so the replacement's fog changed
 * from frame to frame at a fixed camera.
 *
 * It is read instead from the two values `gSPFogPosition` carries, latched
 * where the display list writes them. That command has exactly one emitter in
 * the game - tree-wide, `grep -rn "gSPFogPosition" src/` names bgfog.c:632
 * and bgfog.c:637 and nothing else - and `fogSetRenderFogColor` returns
 * without writing it when `g_FogSkyIsEnabled` is clear. So a level that fogs
 * issues it, a level that does not never does, and the data decides here for
 * the same reason it decides for the game's own geometry.
 *
 * `seen` is whether such a command has ever been observed, `fm` and `fo` are
 * its two signed halves, and `age` is how many display-list frames ago it was
 * (0 = this frame). Returns nonzero when the replacement is to be fogged, and
 * then fills `start` and `end` with the NDC z at which the band begins and
 * saturates - which is what the fm/fo pair MEANS, and what a report should
 * print rather than the raw halves alone.
 *
 * The per-vertex value itself is deliberately NOT computed here: the renderer
 * already owns that arithmetic for the game's own triangles, and the whole
 * point of this contract is that the replacement goes through the same one.
 *
 * A function rather than a condition at the seam so the rule can be tested
 * with no GL context and no display list. */
int sl_wa_fog_band(int seen, int fm, int fo, unsigned age,
                   float *start, float *end);

/* #68 WHETHER THE LEVEL'S OWN FOG BAND APPLIES TO A REPLACEMENT BACKGROUND.
 *
 * THE GAME'S FOG IS A FADE INTO ITS OWN BACKDROP, AND THE GAME KEEPS THE TWO
 * EQUAL. One value feeds both: `g_CurrentEnvironment`'s three bytes are what
 * the backdrop is filled with and what `fogSetRenderFogColor` (bgfog.c:620)
 * hands to SetFogColor, and the environment lerp moves the pair together.
 * Measured on the Dam in ONE `SL_FOG_DBG` line: `fill=103163` - (16,49,99),
 * the 5551 quantisation - beside `fogcol=103060ff` - (16,48,96), which is
 * also the RGB the run's own `env-rgb` event records. So distance does not
 * fade toward "a fog colour"; it fades into THE PICTURE BEHIND IT, and that
 * identity is why the original shows no seam where its band saturates.
 *
 * A REPLACEMENT BACKGROUND TAKES THE GAME'S BACKDROP OFF SCREEN. The same
 * fact the suppression is armed by: with a replacement resolved, what stands
 * behind the remastered world is the replacement and the stage sky composed
 * beneath it (see `sl_wa_compose_skydome`), and the level's environment
 * colour is on screen nowhere. Handing the replacement the level's band
 * therefore fades the remastered world toward a backdrop THAT IS NO LONGER ON
 * SCREEN, and wherever the band saturates the picture snaps to that colour
 * exactly. (Until the #68 console capture this paragraph said the replacement
 * IS the backdrop and so carries its own sky; the capture showed the stage sky
 * is drawn under it. That changed which sky is behind the world, not the
 * finding here: the level's colour is behind neither.)
 *
 * WHAT THAT COST, measured at the playtest's outdoor Dam mark (2026-09-25,
 * eye 11648.3 60.3 1000.1, room 53, theta 127.4, verta 22.3; teleport only,
 * SL_VI_CATCHUP=0, the pinned seed, invincible,invisible,maxammo, read 62):
 *
 *   - the playtest's own marked centre pixel is (16,47,94) at window depth
 *     0.999965 - 13794.8 eye units out, OPAQUE, depth written. It is the
 *     level's fog colour, on the replacement's own geometry;
 *   - the per-pixel fog factor, recovered from a fog-on / fog-off pair at
 *     that camera, is 0.000 above the distant panorama's rim and 0.98..1.000
 *     below it, with the whole transition inside about two screen rows. It is
 *     a MASK, not a gradient: the band is the outer one percent of a 1/w
 *     buffer, the near sky shell sits inside it and the far panorama shell
 *     beyond it, and nothing of this asset is in between;
 *   - the population below that rim is one mesh - hiding the opaque distant
 *     panorama moves 213153 pixels - and the source's own fade strip along
 *     the rim is a further 56894. The strip is the source's authored
 *     transition and it is not what paints the wall;
 *   - unfogged, that panorama reads (131,155,188) .. (91,129,171) down one
 *     column, and the same pixels are IDENTICAL with the vertex colour held
 *     at white, so the shading is in the asset's own images;
 *   - the remaster's own distant terrain, read off the console rather than
 *     remembered, is (110.5,147.0,174.3) with sd (13.7,16.9,20.3), and its
 *     unoccluded sky is (120.7,163.4,195.1). The replacement UNFOGGED is
 *     inside that; the replacement FOGGED is (16,48,96), which no distance
 *     in any console capture comes near.
 *
 * So the replacement already carries the remaster's atmospheric perspective,
 * baked where the remaster put it, and the level's band is a SECOND
 * atmosphere laid over it toward the wrong destination. The near field is
 * not what this gives up: at the Dam's gate yard the band moves 0 pixels of
 * the near yard and 190039 of the distance, which is the population above.
 *
 * WHY THIS CONDITION AND NOT A LEVEL NAME OR A COLOUR. It is the replacement's
 * own presence, the same fact the suppression consumes, so the two can never
 * disagree about whether the game's backdrop is on screen.
 * Every degenerate case falls out of it: a stage with no replacement fogs
 * exactly as it does today; a missing, invalid or unuploadable replacement
 * leaves the game's own backdrop drawing and its own band applying; a
 * presentation that selects no world assets resolves neither. No item path,
 * no level id, no mesh index and NO COLOUR appears in it - nothing here
 * chooses what the distance should look like, it only stops overwriting what
 * the asset already says.
 *
 * REFINED 2026-09-26 - the replacement is NOT unfogged in the remaster, and
 * this rule is unchanged by that. "Carries the remaster's atmospheric
 * perspective, baked" above was half right: the replacement's own pixel
 * programs fog it AT RUNTIME, toward the REMASTER's per-environment colour and
 * range (SLEN, below), not the level's. Measured on the Dam: the play-visible
 * curved ring was the source's translucent rim strip blending an UNFOGGED
 * panorama into the sky, where the remaster blends that same strip - same
 * blend, same alpha, same depth state - from a panorama its programs have
 * already fogged to the stage colour. So the level's band still does not
 * apply here; the remaster's own does, through SL_WA_MAT_FOG and SLEN.
 *
 * `background_replaced` is whether the pass being configured is a resolved,
 * loaded and validated replacement for this stage's backdrop. Returns nonzero
 * when the level's own band applies to it.
 *
 * A function rather than a condition at the seam, for the reason the aperture,
 * the band and the scene-assembly rule are: it is then testable with no GL
 * context and no display list. */
int sl_wa_fog_level_applies(int background_replaced);

/* #68 SCENE ASSEMBLY: WHETHER THE STAGE SKY IS PART OF THIS COMPOSITION.
 *
 * It is, in both states, and the previous answer here was wrong.
 *
 * WITH NO REPLACEMENT the skydome is ADDITIVE, as its class says: drawn as
 * well as the game's own picture, over the game's own backdrop.
 *
 * WITH A REPLACEMENT it is the replacement's COMPANION, drawn first and
 * beneath it. This used to answer no, on the reasoning that a replacement
 * "IS the stage's backdrop and carries its own sky" - a reading of the two
 * classes' contracts, never a measurement. The console measured it (#68,
 * instrumented draw log, the Dam in the remaster's own presentation):
 *
 *   - LOAD: the remaster resolves the stage's skydome item INSIDE the load of
 *     its background item - the only insertion in an otherwise fixed
 *     four-line pattern, twice, once per Dam load. There is no original-
 *     generation skydome at all; the sky is the background's companion.
 *   - ORDER: after the frame's black clears the skydome is the FIRST world
 *     draw (opaque replace, depth tested and written), and the background's
 *     draws follow it.
 *   - OWNERSHIP: skipping only the skydome's pixel shader in a matched pair
 *     turned the bright upper hemisphere into a flat dark backdrop and moved
 *     658731 of 1009584 pixels. The background has no geometry above 39.66
 *     degrees of elevation; the sky above its rim IS the skydome.
 *
 * Suppressing the sky left exactly that region to the game's own backdrop
 * fill, the dark (16,48,96) "reversed ring" above the panorama's rim.
 *
 * WHY NO CONDITION REMAINS. The relationship is the provider's own: the two
 * classes are resolved by the same stage item path, the sky is drawn at the
 * frame's first world triangle, and the replacement is drawn after it, depth
 * tested, so it overlays the sky as authored and its own translucent rim
 * shows the sky through it. No coverage test, item path, level id, mesh index
 * or colour is needed to say that, so none appears. A stage with a
 * replacement and no skydome item simply resolves no sky.
 *
 * `background_replaced` is whether a replacement background is resolved,
 * loaded and validated for this stage; it is kept so the rule states both
 * cases where a test can see them. Returns nonzero when the stage skydome is
 * part of the composition.
 *
 * A function rather than a condition at the seam, for the reason the aperture
 * and the fog band are: the rule is then testable with no GL context. */
int sl_wa_compose_skydome(int background_replaced);

/* #77 THE BACKDROP UNDER A REPLACEMENT ON A STAGE WITH NO SKY.
 *
 * Where the environment carries no cloud layer the game draws no sky: its
 * sky pass fills the whole view with the environment colour (skyRender's
 * !Clouds branch, sky.c) and the rooms draw over that. The fill is the
 * cartridge's backdrop. The remaster's frame begins with a black clear (the
 * console's draw log at the Dam, above: "after the frame's black clears the
 * skydome is the FIRST world draw"), so wherever its background has no
 * surface the remaster shows black. Facility's conveyor exit is such a place:
 * open in the remaster's item, backed by a black face on the cartridge, and
 * green (the fill) under the replacement until this rule.
 *
 * Nonzero when the pass about to draw is a replacement (`background_drawn`),
 * no stage sky was drawn beneath it this frame (`sky_drew` zero) and the
 * stage's backdrop is the flat fill (`flat_backdrop`): the aperture is then
 * cleared to black before the first mesh. A stage with a sky, or a frame
 * whose dome drew, keeps whatever is there - the Dam is unchanged by
 * construction. A function rather than a condition at the seam, for the
 * reason the rules above are: it is testable with no GL context. */
int sl_wa_backdrop_black(int background_drawn, int sky_drew, int flat_backdrop);

/* #81 A FOGLESS STAGE KEEPS THE CARTRIDGE'S RANGE AND BACKDROP.
 *
 * The remaster's own environment metadata (SLEN: its fog colour and far end,
 * which also carry its view range) extends the rows of the game's fog table.
 * A stage the game draws from its FOGLESS table has no such row: the game
 * keeps its 15 .. 10000 range and fills the view with the environment colour
 * before the rooms (bgfog.c's fogless branch, sky.c's flat fill). Bunker 1
 * is one. Its remaster item still carries the exterior - a sky panorama and
 * a cloud sheet 14000 world units out - and the remaster does not show it:
 * up the vent shaft, behind its grate, the remaster shows a uniform dark
 * navy field (mode 3, 10, 53, private emulator at playtest mark 1), the
 * cartridge's own fill colour (0, 16, 64) and not black and not clouds. This
 * renderer drew that exterior (clamped to the far plane, or in the #78 far
 * range) and cleared the backdrop to black, so bright clouds stood behind
 * the grate (playtest mark 1).
 *
 * Nonzero when a replacement is drawn (`background_drawn`) on a stage whose
 * game environment row is none (`env_id` negative - sl_fog_environment_id's
 * answer for the fogless table). The pass then draws nothing past the game's
 * far plane (no clamp, no far range) and leaves the game's own fill as its
 * backdrop (no black clear). Every stage with a fog row - the Dam, Facility,
 * Runway, Surface - is unchanged by construction. The renderer's
 * SL_BG_FOGLESS=0 draws as before. Pure. */
int sl_wa_fogless_range(int background_drawn, int env_id);

/* #78 THE REPLACEMENT BEYOND THE GAME'S FAR PLANE.
 *
 * A replacement background is larger than the game's frustum: Runway's
 * reaches clip w 43000 against a far plane of 15000 (its environment row's
 * FarFog, at a visibility scale of 1.0). The background pass used to CLAMP a
 * clip z past w to w, so all of that geometry landed at depth 1.0 and the
 * depth test could no longer order it: whichever mesh came later in the
 * item's draw order covered the rest, and a mountain or a building crossing
 * the far plane as the player moved appeared or vanished whole.
 *
 * The pass now draws such a frame in two depth ranges. First the part of the
 * item beyond the game's far plane, under the same x, y and w with z
 * re-mapped so the near plane sits just inside the game's far plane and the
 * far plane just beyond the item's furthest vertex - the depth test orders it
 * among itself. Then the depth is cleared and the item is drawn again under
 * the game's own projection, unclamped, so everything within the far plane
 * is drawn and leaves exactly the depth the game's props, doors and
 * characters test against, and everything beyond is clipped away and keeps
 * the colour the first range gave it.
 *
 * sl_wa_far_plane recovers the far plane from the world-to-clip matrix in
 * force (row vectors, c = p * C): for a perspective after an affine view,
 * clip z = k w - m in every column, so k = -C[i][2] / C[i][3] for any input
 * axis i with a w term and m from the translation row; near = m / (k - 1)
 * and far = m / (k + 1), in the units clip w is in. Returns 0 (and writes
 * nothing) for a matrix with no perspective or no positive near/far.
 *
 * sl_wa_far_range gives the first range's z: c2 = k1 * w - k2 maps w = from
 * to the near plane and w = to to the far plane.
 *
 * THE OVERLAP (#80). The first range starts SL_WA_FAR_OVERLAP of the far
 * plane in, and both ranges draw what lies between. It was a thousandth, and
 * on Surface that left two long, thin streaks of sky through the panorama
 * shell (the reported "rays"): the second range's far clip is where the
 * game's clip z meets w, and z - w there changes by only 2n / (f - n) of w
 * per unit of depth (0.0016 on Surface: near 2, far 2522.6 in clip w), so
 * the rounding of z and w moves that clip by a large share of a unit. With
 * the first range drawn only and the second only (one mesh, the shell), the
 * streaks are exactly the strip neither drew, 1 to 5 pixels wide along a
 * line of constant depth; the clip lands short of the recovered far plane by
 * up to about 0.3% there. Starting the first range at 0.995 or 0.99 of the
 * far plane leaves no such pixel at the playtest mark (0 of 1240); 0.99 is
 * taken, three times the shortfall seen. Where both ranges draw, the second
 * draws last over a cleared depth buffer, so its picture stands - for an
 * opaque draw. A BLENDED one (SL_WA_BLEND_BLENDS: translucent, and a
 * cutout's soft edge) is laid twice there - measured on Runway at the
 * replay's mountain mark, the canopy edges of distant trees a step darker
 * over 146 pixels with the whole overlap - so it keeps the thousandth it
 * always had, SL_WA_FAR_OVERLAP_BLEND. The one blended draw that carried
 * the streaks, the panorama shell (SL_WA_MAT_PANORAMA), is not split
 * between the ranges at all (sl_gfx_dl.c, bg_far_shell). */
#define SL_WA_FAR_OVERLAP       0.99f
#define SL_WA_FAR_OVERLAP_BLEND 0.999f
int  sl_wa_far_plane(const float *C, float *zfar);
void sl_wa_far_range(float from, float to, float *k1, float *k2);

/* #78 THE NATIVE FILL - the game's own room triangle where the replacement
 * has no surface.
 *
 * WHAT IT CLOSES. A replacement background is suppression's warrant: once it
 * has drawn, every room triangle of the game's is dropped (sl_gfx_dl.c,
 * emit_tri_slots). Where the replacement reproduces the room that is right.
 * Where it leaves a surface out it is a hole: Runway's bunker door opening
 * (the marked pose, room 4) is cut a few units taller and wider in the remaster
 * than in the game - the wall the game draws above and beside the door
 * (world y -246 .. -239 above it) has no counterpart, and the sky stands in
 * it. The remaster itself shows the same strip (measured on the console
 * build at the same pose), so the source has nothing to fill it with; the
 * game does.
 *
 * THE RULE. A room triangle is REGISTERED when each of its three corners
 * lies within SL_WA_FILL_REGISTER world units of a depth-writing surface of
 * the replacement - a triangle the replacement reproduces, give or take the
 * registration the two geometries have (interior rooms coincide to within
 * about five units; a remodelled cliff sits tens of units away and never
 * registers), and HOLED when some point of it - a barycentric grid of
 * SL_WA_FILL_GRID along each edge - lies at least SL_WA_FILL_HOLE world
 * units from every such surface: the strip above the door is seven units
 * tall and the crack beside it three and a half, so each has points that
 * far from the remaster's cut edges; a wall top or barrier edge the
 * remaster moved by a unit or two leaves a sliver whose every point is
 * within that, and that sliver is the replacement's own shape, not a
 * hole. The radius was swept (1.5, 2.5, 3.5): at 3.5 the crack beside the
 * door reopens; at 1.5 the Dam's barrier edge draws a sliver; 2.5 closes
 * the doorway with the Dam's and Facility's witness poses unchanged but
 * one barrier corner (159 pixels). What stays at that radius on Runway is
 * what the game's walls are and the remaster's are not: a wall top two
 * units proud of the remaster's roof draws as a ledge along its
 * silhouette, a railing brace the remaster placed elsewhere shows behind
 * the remaster's. A registered, holed triangle is not
 * suppressed: it is drawn SL_WA_FILL_PUSH world units behind its own plane,
 * along its normal away
 * from the eye, so wherever the replacement has a surface within that
 * distance the replacement wins the depth test and the game's triangle is
 * invisible, and only where the replacement has nothing - the strip above
 * the door - does the game's surface show. The push is along the normal
 * rather than the line of sight so a floor two units above the
 * replacement's stays hidden at a grazing angle too. Only an opaque,
 * depth-writing, non-decal triangle fills: a blended or alpha-tested one
 * (a fence, a window) would show through the replacement's own holes.
 *
 * THE GRID is the pure half, here: sl_wa_fill_set builds, over the
 * replacement's depth-writing triangles (every material but ALPHA), a
 * hashed uniform grid of `cell` item units in SL_WA_FILL_LEVELS levels,
 * each SL_WA_FILL_COARSEN times coarser than the one before: a triangle
 * goes to the first level where it spans at most SL_WA_FILL_LARGE cells on
 * every axis, and one wider than that at the coarsest level is kept in a
 * list of its own. It returns 1 when it (re)built and 0 when the same item
 * and cell were already in hand, and takes NULL to release.
 * sl_wa_fill_distance returns the distance from an item-space point to the
 * nearest such surface, or `limit` when none lies within it: at every
 * level it visits the cells the box point +- limit touches (a triangle is
 * entered in every cell of its level its bounds cover, so any triangle
 * within limit is met), then the list; the grid needs cell >= 2 * limit for
 * the eight cells around the point to cover the search box, which the
 * caller keeps.
 *
 * WHY LEVELS (measured on Surface, 2026-10-05): with one level and a list,
 * the Surface item put 2272 of its 39199 triangles - the mountains and the
 * snowfields, up to 77 cells wide - on the list, and every query tested all
 * of them after its cells. A decision (sl_gfx_dl.c: seven corner and middle
 * samples and a 45-sample hole grid) is up to 52 queries, so each cost
 * about 2 ms, the 2200 triangles first seen at a pose stalled the frame 4.3
 * seconds, and every room entering view hitched for as long as its
 * triangles took. With cells of 16 and 256 cells the list is empty on
 * Surface, Facility and the Dam and holds 12 on Runway, and a query visits
 * at most eight cells per level. The
 * caller (sl_gfx_dl.c) decides per triangle, caches the decision by the
 * triangle's own room-space coordinates, and pushes. SL_BG_FILL=0 draws
 * without it. */
#define SL_WA_FILL_REGISTER 8.0f      /* world units: a corner registers within this */
#define SL_WA_FILL_HOLE     2.5f      /* world units: a registered triangle fills only when some
                                       * point of it lies this far from every replacement surface */
#define SL_WA_FILL_PUSH    12.0f      /* world units: a registered triangle sits this far back */
#define SL_WA_FILL_GRID     8u        /* the sample grid along each edge for the hole test */
#define SL_WA_FILL_LARGE   8u         /* cells: a wider triangle goes to the next level, or the list */
#define SL_WA_FILL_LEVELS  3u         /* grid levels: cell, cell x 16, cell x 256 */
#define SL_WA_FILL_COARSEN 16u        /* each level's cell over the one before */
int   sl_wa_fill_set(const struct sl_wa_asset *a, float cell);
float sl_wa_fill_distance(const float *p, float limit);
/* #83 THE REPRODUCTION IS IN THE TRIANGLE'S OWN PLANE. sl_wa_fill_distance
 * counting only the replacement's triangles whose plane lies within
 * SL_WA_FILL_ALIGN of the unit normal `n` (either facing). A room triangle
 * REGISTERS on that distance (sl_gfx_dl.c nf_registered): a surface the
 * replacement reproduces lies in the same plane, while a surface it has
 * remodelled into another shape - Frigate's gangway, a sloped plank in the
 * game and an open stair of level treads in the remaster, 33 degrees apart -
 * only passes near it. Before, the plank registered on the treads, read as
 * holed between them, and was drawn behind them: the game's diamond plate
 * stood in the stair's open gaps (playtest run 20261006-173108 marks 1-2).
 * 15 degrees, D-033's coincidence angle (tools/worldasset/gameshade.py). The
 * hole test keeps the unaligned distance. */
#define SL_WA_FILL_ALIGN 0.966f
float sl_wa_fill_distance_aligned(const float *p, const float *n, float limit);
unsigned sl_wa_fill_counts(unsigned *large);    /* triangles gridded; large listed */

/* #81 A HOLE THE REPLACEMENT BACKS IS ITS OWN OPENING.
 *
 * A registered, holed triangle was drawn wherever the replacement's surface
 * lay more than the push behind it. On Bunker 1 the game's solid stair steps
 * register on the remaster's open stair (cut-out grating treads, open risers)
 * and are holed in its gaps; pushed 12 units back they drew as bright
 * stripes across the stair (playtest mark 5), over the remaster's next
 * tread and the floor it shows through the gaps. On Runway and the Dam the
 * same kind of hole lined crate rims and the road's edge with slivers.
 * Runway's strip above the bunker door - what the fill exists for - looks
 * out at terrain 4900 world units away; the game's own door stands a few
 * units behind it, and the door is not the replacement's.
 *
 * So the fill is drawn only where the replacement leaves the hole open: from
 * the hole's point (`hq`, the point of the triangle farthest from every
 * replacement surface, found by the hole test) along the line of sight away
 * from the eye (`eye`), sphere-traced through the fill's own distance grid
 * in steps of at most SL_WA_FILL_OPEN_STEP, no replacement surface within
 * SL_WA_FILL_OPEN_HIT of the ray before SL_WA_FILL_OPEN world units. Item
 * units in and out (`sc` world units per item unit); 1 open, 0 backed. Asked
 * per frame of the registered, holed triangles alone (the eye moves). A depth
 * test cannot ask it: the depth buffer holds the game's door as well. */
#define SL_WA_FILL_OPEN      1000.0f   /* world units: nothing of the replacement this far */
#define SL_WA_FILL_OPEN_STEP   32.0f   /* world units: the trace's largest step (grid bound) */
#define SL_WA_FILL_OPEN_HIT     1.0f   /* world units: this near a surface is a hit */
int sl_wa_fill_open(const float *eye, const float *hq, float sc);
/* The grid's distance query skips a triangle whose bounding box lies
 * farther than the best distance found so far; the box is widened by
 * SL_WA_FILL_BOX_SLACK of the item's largest coordinate (past the rounding
 * of a closest point at that magnitude) and a box must clear the best
 * squared distance by SL_WA_FILL_BOX_MARGIN, so no triangle that could
 * lower it is skipped and every answer is the exhaustive test's. */
#define SL_WA_FILL_BOX_SLACK  (1.0f / 262144.0f)
#define SL_WA_FILL_BOX_MARGIN 0.001f

/* #83 A ROOM THE REPLACEMENT REMODELLED WHOLE TAKES NO FILL: 1 when the
 * presentation withholds the native fill from `room` of the stage `item`
 * under the active set (sl_presentation_fill_withheld - Frigate's boarding
 * boat, room 57), and the caller suppresses every triangle of the room as it
 * did before the fill. SL_BG_FILL_WITHHELD=0 answers 0 everywhere. */
int sl_wa_fill_withheld(const char *item, int room);

/* #96 THE GAME'S DETAIL IN A NAMED ROOM: 1 when the presentation keeps the
 * game's translucent room triangles that the replacement has no surface for
 * in `room` of the stage `item` under the active set
 * (sl_presentation_native_kept - Cradle's room 7, the shaft's ladder), and
 * the caller draws such a triangle where it stands (sl_gfx_dl.c
 * nf_room_kept). SL_BG_KEEP_ROOMS=0 answers 0 everywhere. */
int sl_wa_native_kept(const char *item, int room);

/* D-035 THE GAME'S FADE, BY STAGE: 1 when the stage `item` draws the game's
 * fade where the replacement has no surface (sl_gfx_dl.c nf_fade_kept) under
 * the active set - sl_presentation_game_fade names the stages (Frigate) -
 * and 0 on every other stage, whose fade triangles are suppressed with the
 * rest of its rooms. SL_BG_FADE_STAGES=0 answers 1 on every stage with an
 * item (the earlier rule); SL_BG_FADE=0 still draws no fade anywhere. */
int sl_wa_game_fade(const char *item);

/* D-039 SIGHTLINE'S OWN SEA: the sea program's form for the stage `item`
 * under the active set - 0 the game's sea, else the form to draw
 * (sl_presentation_own_sea names the stage; SL_XBLA_SEA picks the form: 0
 * the game's sea, 1 the layered image and the horizon alone, 2 - the
 * default - with the sky's reflection, 9 a diagnostic; sl_gfx_dl.c). */
#define SL_WA_SEA_DEFAULT 2
int sl_wa_own_sea(const char *item);

/* #68 THE REMASTER'S STAGE FOG ("SLEN") - ENVIRONMENT METADATA.
 *
 * WHAT IT IS. The remaster fogs its replacement background with a per-
 * ENVIRONMENT fog colour and fog end that live in neither the world-asset
 * items nor the ROM: they are an extension the remaster appended to each row
 * of the game's own environment table (`fog_tables[]`, bgfog.c). The
 * preparation step derives them from the user's own executable
 * (tools/worldasset/xexenv.py) into this small generated file, and the
 * renderer reads only this - it never sees an executable.
 *
 *   <root>/xbla/environment.slen        (generated, gitignored, never shipped)
 *
 * KEYED BY THE GAME'S OWN ENVIRONMENT IDENTITY - the EnvironmentRecord Id the
 * game itself selected (`fogLoadLevelEnvironment`: stage id, +900 for the
 * intro cinema, +100..400 for the alternate and multi-player rows) - so the
 * selector is the game's and no stage, level or state is named here.
 *
 * THE DISTANCE UNIT, measured rather than assumed: the remaster's view-space
 * distance is the game's WORLD distance after the stage's level scale and
 * BEFORE its visibility scale. On the Dam the ring's vertex program's
 * modelview is a uniform 4.28009 x rotation (the level scale) with no 0.2 in
 * it; one ring vertex measured 71403.7 units from the eye through that
 * matrix, and its 16682.8 asset units x 4.2801 is 71403.9. So a distance
 * taken in this renderer's visibility-scaled eye space is divided by the
 * visibility scale before it meets fog_start / fog_end.
 *
 * Little-endian, bounded like SLWA:
 *
 *   +0   'S' 'L' 'E' 'N'
 *   +4   u32 version        SL_WA_ENV_VERSION
 *   +8   u32 row_count      1 .. SL_WA_ENV_MAX_ROWS
 *   +12  u32 row_size       SL_WA_ENV_ROW
 *   +16  u32 file_size      exactly the file's length
 *   +20  u32 reserved[3]    0
 *   row  u32 environment id (unique)   f32 fog_start   f32 fog_end (> start)
 *        u8 red, green, blue           u8 flags (0)
 *
 * A missing file is the unfogged replacement this renderer drew before it
 * existed; an invalid one is the same, reported once. */
#define SL_WA_ENV_VERSION   1u
#define SL_WA_ENV_HEADER    32u
#define SL_WA_ENV_ROW       16u
#define SL_WA_ENV_MAX_ROWS  512u

struct sl_wa_env {
    unsigned id;               /* the game's EnvironmentRecord Id           */
    float fog_start, fog_end;  /* world units, after level scale            */
    unsigned char rgb[3];      /* the fog colour, 8-bit, as authored        */
};

/* Validate and decode a whole SLEN file already in memory into `rows`
 * (capacity `cap`). Returns SL_WA_OK and sets *count, or a reason. */
int sl_wa_env_parse(const unsigned char *buf, unsigned long len,
                    struct sl_wa_env *rows, unsigned cap, unsigned *count);

/* The row for environment `env_id` under the active set, AS PRESENTED
 * (sl_wa_env_presented, D-025), or NULL: no set, no
 * file, an invalid file, or no such row. The file is read at most once per
 * selected set; a call is a search of at most SL_WA_ENV_MAX_ROWS rows, and
 * the renderer asks once per frame, never per vertex. */
const struct sl_wa_env *sl_wa_env_lookup(int env_id);

/* The GL LINEAR(0..1) fog coordinate for a point `d` world units from the
 * eye under row `e`: 1 - saturate((fog_end - d) / (fog_end - fog_start)).
 * 0 is the material colour untouched, 1 is the fog colour. */
float sl_wa_env_fog_coord(const struct sl_wa_env *e, float d);

/* D-025 THE PRESENTED ROW: `authored` as set `set` draws it - the fog end the
 * presentation policy gives that row (sl_presentation_fog_end), and the
 * authored row exactly when `authentic` is nonzero. The colour, the start and
 * the id are always the authored ones, and so is the end if the presented one
 * would not lie beyond the start. Pure, so the self-test pins it.
 *
 * The provider keeps both: the authored table as the set's metadata carries
 * it, and the presented table sl_wa_env_lookup answers with, so every
 * consumer of a row - the replacement background, and the game's props and
 * characters fogged by the same row (sl_wa_object_fog_env) - draws one range.
 * SL_XBLA_AUTHENTIC_FOG=1 (developer diagnostic, read once) presents every
 * row as authored. */
struct sl_wa_env sl_wa_env_presented(const struct sl_wa_env *authored,
                                     const char *set, int authentic);

/* D-030 THE GAME'S ATMOSPHERE. Whether the active set draws environment
 * `env_id`'s atmosphere from the GAME (sl_presentation_game_atmosphere):
 * the stage-fog colour the game's environment holds in place of the row's
 * authored colour, and the sky dome shaded as the game shades its own cloud
 * layer (sl_wa_sky_shade). 0 with no active set, for a row the policy does
 * not name, and under SL_XBLA_GAME_ATMOSPHERE=0 (developer switch, read once:
 * the set's own colour and the dome as authored). The renderer asks with the
 * environment the game selected; the colour itself is read from the game
 * each frame (sl_world_assets.c, sl_wa_game_atmosphere), so a row the game
 * changes is followed - the presentation keeps no colour of its own. */
int sl_wa_env_game_atmosphere(int env_id);

/* D-041 PROPS PAST THE FAR-FOG CUT. Whether the active set draws the game's
 * props past the game's far-fog cut while the game holds environment
 * `env_id` (sl_presentation_prop_fog_drawn): the prop is drawn and the row
 * fogs it per vertex, as it fogs the background. 0 with no active set, for a
 * row the policy does not name, and under SL_XBLA_FOGGED_PROPS=0 (developer
 * switch, read once: the game's cut). The renderer answers the game through
 * sl_gfx_prop_fog_drawn, and only while the row also fogs the per-object
 * draws (sl_gfx_native_stage_fog) - a prop is never drawn past the cut
 * without the fog that hides it. */
int sl_wa_env_prop_fog_drawn(int env_id);

/* D-030 / D-034 WHETHER THE STAGE'S SKY DOME IS SHADED AS THE GAME'S CLOUD
 * LAYER, for the stage whose item path is `item` while the game holds
 * environment `env_id`. With a row (`env_id` >= 0) this is exactly
 * sl_wa_env_game_atmosphere(env_id). Without one - the fogless record, which
 * still carries a sky colour and a cloud layer - it is the policy's
 * per-stage answer (sl_presentation_fogless_atmosphere: Frigate in the xbla
 * set), and the renderer then reads the colours from the record the game
 * loaded (sl_wa_game_atmosphere). 0 with no active set, for an item the
 * policy does not name, and under SL_XBLA_GAME_ATMOSPHERE=0. Nothing but the
 * dome's shade follows from it: a fogless stage keeps its range and fill. */
int sl_wa_stage_game_atmosphere(const char *item, int env_id);

/* D-031 A BORROWED SKY. The skydome item the active set draws for the stage
 * whose item path is `item` while the game holds environment `env_id`, when
 * that stage has no sky of its own (sl_presentation_sky_borrow), and in *row
 * the environment row whose atmosphere shades it - Bunker 1 (`bunker`) draws
 * `surface` under row 36 in the xbla set. Asked only while the game holds no
 * environment row (`env_id` negative: the fogless record, sky.c's flat
 * fill); a stage with a row draws its own. NULL with no active set, for an
 * item the policy does not name, and under SL_XBLA_BORROWED_SKY=0 (developer
 * switch, read once: the game's fill, as before). The renderer draws the
 * borrowed dome with the row's colours and cloud layer read from the game's
 * own table (sl_world_assets.c), and nothing else of that row: the stage's
 * range, fog and fill stay its own (sl_wa_fogless_range). */
const char *sl_wa_sky_borrow(const char *item, int env_id, unsigned *row);

/* D-044 A LENT DOME. The skydome item the active set draws for the stage
 * whose item path is `item` when the source carries no dome for it and the
 * stage keeps an environment row of its own (sl_presentation_sky_lent) -
 * Streets (`streets`) and Depot (`depot`) draw `frigate` in the xbla set.
 * Only the dome is lent:
 * the renderer shades it with the stage's own atmosphere and cloud layer, read
 * live from the game (sl_wa_stage_game_atmosphere, sl_wa_sky_cloud), exactly
 * as a stage with a dome of its own. NULL with no active set, for an item the
 * policy does not name, and under SL_XBLA_LENT_SKY=0 (developer switch, read
 * once: the game's own cloud layer, as before). */
const char *sl_wa_sky_lent(const char *item);

/* `row` with its colour replaced by `rgb` (0..255 each, as the game's
 * environment holds it; rounded and clamped). Range, start and id are the
 * row's. Pure. */
struct sl_wa_env sl_wa_env_coloured(const struct sl_wa_env *row, const float rgb[3]);

/* D-030 THE GAME'S CLOUD SHADE, sky.c's own: the colour skyChooseCloudVtxColour
 * gives a point of the cloud layer seen along direction `d` (any length,
 * y up) from the eye -
 *
 *     frac  = 1 - min(1, 2 * d.y / |d.xz|)        (skyIsScreenCornerInSky)
 *     shade = fog + cloud * (1 - fog / 255) * (1 - frac)
 *
 * so the full cloud colour over the fog colour from about 26.6 degrees up
 * (tan = 0.5), and the fog colour itself at the horizon and below (frac is
 * held to 0..1 here; the game draws no cloud below the horizon). The game's
 * combiner then takes lerp(fog, shade, texel), which the renderer does with
 * the dome's own image as the texel. Pure; `out` 0..255. */
void sl_wa_sky_shade(const float fog[3], const float cloud[3], const float d[3],
                     float out[3]);

/* The same shade for a cloud fraction `frac` given directly (held to 0..1).
 * sl_wa_sky_shade is this with frac from the direction. Pure. */
void sl_wa_sky_shade_frac(const float fog[3], const float cloud[3], double frac,
                          float out[3]);

/* D-046 THE GAME'S CLOUD LAYER WHERE IT LIES ON THE SCREEN. skyRender draws
 * its cloud polygon from the top of the view down to a horizon EDGE, shaded
 * by the corners' cloud fractions, and leaves the fill colour (the fog
 * colour) below it; with the environment's WaterConcavity that edge stands
 * well above the true horizon (Streets: 25 rows; measured, the fill from
 * game row 90.6 of 10..230 down, looking along a street). A direction's
 * fraction (sl_wa_sky_shade) reaches the fog colour only AT the horizon, so a
 * dome shaded by it stands lighter than the fog in that band - where the
 * game's own picture is the fog colour, and where a fully fogged prop that
 * the game's far plane cuts appears as a fog-coloured silhouette.
 *
 * `rows` and `fill` are sl_wa_sky_screen's (src/native/sl_world_assets.c:
 * the game's own corners, read each frame): the top row and the height, per
 * side (left 0, right 1) two (row, fraction) anchors, linear between them
 * and continued past them, and whether the far side of an anchor is the
 * fill. sl_wa_sky_screen_frac answers the fraction at a point of the
 * picture (`ndc_x`, `ndc_y`, -1..1, y up): the row is the top plus
 * (1 - ndc_y) / 2 of the height, each side's fraction is taken at that row
 * (1 in the fill), and the two are interpolated across by ndc_x. Pure; held
 * to 0..1. sl_wa_sky_screen_edge gives the edge's height in NDC at the two
 * sides when both sides have the fill below it (1), else 0. Pure. */
double sl_wa_sky_screen_frac(const float rows[10], const int fill[4],
                             float ndc_x, float ndc_y);
int sl_wa_sky_screen_edge(const float rows[10], const int fill[4], float ndc_y[2]);

/* D-046 is drawn: a set is active and SL_XBLA_LENT_SKY_EDGE is not 0
 * (developer switch, read once). Only a LENT dome (D-044) is shaded this
 * way; every other dome keeps its own rule. */
int sl_wa_sky_lent_edge(void);

/* #77 THE BLENDED ROW: the stage fog while the game blends its environment
 * from one row toward the next (fogSwitchToSolosky2, bgfog.c - Facility's
 * toxic gas: the main row's far fog and colour lerped toward the ALT row's by
 * the gas timer, `t` 0..1). The remaster carries an extension for the ALT
 * row as for every other (Facility's: fog end 1000 toward (64, 128, 64),
 * against the main row's 5000 toward (16, 32, 1)), so the replacement and
 * the objects in front of it are fogged by the same lerp of the two rows'
 * ranges and colours: start, end and each colour channel moved `t` of the
 * way from `main` to `alt`, the colour rounded to the nearest step. The id
 * stays the main row's. `t` at or below 0 (or not a number) is `main`
 * exactly, at or above 1 is `alt`'s range and colour. Pure, so the self-test
 * pins it. */
struct sl_wa_env sl_wa_env_blend(const struct sl_wa_env *main_row,
                                 const struct sl_wa_env *alt_row, float t);

/* #68 WHICH ATMOSPHERE FOGS THE GAME'S OWN PER-OBJECT DRAWS.
 *
 * The class: props and characters, which the game fogs one object at a time
 * (fogGetPropDistColor, bgfog.c) - their fog colour and alpha are the
 * object's shade lerped toward the level's environment colour by the object's
 * depth, drawn with G_RM_FOG_PRIM_A (the renderer's FOG_CONST).
 *
 * MEASURED on the remaster (Dam gameplay, private emulator, the pixel
 * programs' float constants for every prop and character draw): those draws
 * bind the SAME stage fog the replacement background does - the range in c0
 * and the colour in c1 are that environment row's, value for value - and end
 * in the same tail, rgb = lerp(c1, colour, saturate(|view pos| * c0.x +
 * c0.y)). The per-object term they carry before that tail (c7, a lerp toward
 * c7.rgb by c7.w) is the object's shade and nothing else: over 42k gameplay
 * lines with the object 2500..8000 units in front and 1227 past 8000, c7.rgb
 * is (0,0,0) every time, where the game's own formula would put a prop at
 * 8000 units 72% of the way to (16,48,96). So the remaster fogs this class
 * with its own environment row and does not apply the level's distance fog to
 * it at all.
 *
 * THE RULE is the scene's single atmosphere: where the replacement background
 * is the stage's backdrop (so the level's band does not apply to it -
 * sl_wa_fog_level_applies), the remaster row that fogs the backdrop also owns
 * the distance of the objects standing in front of it. Everywhere else the
 * game's own per-object fog stands: no replacement (ORIGINAL and COMMUNITY
 * select no world assets; a stage with none), missing or invalid metadata,
 * or no row for the environment the game selected.
 *
 * `background_replaced` is the same fact the level-band rule reads; `row` is
 * sl_wa_env_lookup's answer for the game's environment. Returns the row that
 * fogs the class, or NULL for the game's own per-object fog. */
const struct sl_wa_env *sl_wa_object_fog_env(int background_replaced,
                                             const struct sl_wa_env *row);

/* #69 THE DISPLACEMENT one animation record gives one vertex at time `t`
 * (seconds of the frame clock), along that vertex's direction, in the
 * bundle's own units - the contract under MATERIAL ANIMATION above,
 * evaluated exactly as written there. `wave_a` and `wave_b` are the vertex's
 * raw pair. A record whose mode this renderer does not evaluate, or a NULL
 * record, displaces by zero. A pure function of its arguments, so the
 * self-test pins it: the value at a known phase, its period in `t`, that
 * zero amplitude moves nothing, and that the same `t` from any frame rate is
 * the same displacement. */
float sl_wa_anim_displacement(const struct sl_wa_anim *a,
                              float wave_a, float wave_b, double t);

/* #69 seconds of the frame clock for `frames` VI frames - the one place the
 * rate is applied, so the renderer and the self-test agree on it. */
double sl_wa_anim_seconds(unsigned long frames);

/* #69 THE MATERIAL EXTENSION'S SHADE, the contract under MATERIAL EXTENSION
 * above evaluated for one fragment on the host - the reference the
 * renderer's program mirrors instruction for instruction, so the self-test
 * can pin the coordinate composition, the sphere lookup and the weights
 * without a GL context. Images are supplied as callbacks so the test can
 * hand it exact texels: `tex(ctx, which, u, v, rgba)` fills four floats
 * 0..1 for image `which` (0 normal, 1 sphere, 2 offset, 3 base) at (u, v).
 * `frame` is the vertex's X, Y and normal, already in view space; `colour`
 * the vertex colour 0..1; `terms` a mask of SL_WA_MATX_T_* selecting which
 * of the record's terms apply (all three is the contract; the others exist
 * for the renderer's diagnostic control and the test that a term left out
 * reproduces the base alone). Writes rgba 0..1 before fog. */
#define SL_WA_MATX_T_WARP     1u
#define SL_WA_MATX_T_NORMAL   2u
#define SL_WA_MATX_T_REFLECT  4u
#define SL_WA_MATX_T_ALL      7u
typedef void (*sl_wa_tex_fn)(void *ctx, int which, float u, float v, float *rgba);
void sl_wa_matx_shade(const struct sl_wa_matx *m, unsigned terms,
                      const float *frame, const float *colour, float u, float v,
                      sl_wa_tex_fn tex, void *ctx, float *rgba);

/* ---- the parser, exposed so it can be tested without a filesystem -------- */

/* Validate the 128 header bytes alone. `len` is how many bytes are really
 * there (the whole file's length), which is what lets a truncated file be
 * rejected rather than read past. Never reads beyond SL_WA_HEADER bytes of
 * `hdr`, and never dereferences anything else. */
int sl_wa_parse_header(const unsigned char *hdr, unsigned long len,
                       struct sl_wa_asset *out);

/* Validate and bind the tables of a whole file already in memory. `buf` must
 * be `len` bytes and stay alive as long as `out` is used. Fills every table
 * and pointer in `out`; returns SL_WA_OK or a reason, having touched nothing
 * outside `buf`. */
int sl_wa_parse(const unsigned char *buf, unsigned long len,
                struct sl_wa_asset *out);

#endif /* SL_GFX_WORLDASSET_H */
