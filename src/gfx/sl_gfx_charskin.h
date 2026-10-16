/**
 * sl_gfx_charskin.h - the arithmetic of a skinned character replacement (#70):
 * relating a replacement item's bind space to the game's skeleton
 * (sl_chr_derive, declared with the shared types in src/sl_chr_asset.h) and
 * deforming the item by the pose the game animated.
 *
 * CONVENTIONS. Matrices are the renderer's: 16 floats, row-major under a
 * row-vector convention, v' = v * M (see sl_gfx_dl.c, vtx_transform). A game
 * matrix is the one a GROUP node's geometry is drawn under: joint-local
 * coordinates to eye space. An item vertex v bound to item joint j reaches
 * eye space through
 *
 *     S_j = T(-bind_j) * scale(1 / s) * Rz(rot_j) * M_game(mtx_j)
 *
 * - into the joint's own origin, down to game units, turned into the game
 * part's frame, then the game's own matrix - and a vertex takes the weighted
 * sum of its (up to four) joints' results.
 *
 * HOST-CLEAN: plain C, no GL, no game headers; the self-test
 * (-DSL_CHR_SELFTEST) links it with the world-asset provider alone.
 */
#ifndef SL_GFX_CHARSKIN_H
#define SL_GFX_CHARSKIN_H

#include "../sl_chr_asset.h"
#include "sl_gfx_worldasset.h"

/* The per-joint matrices S_j for one item, from the game's decoded matrices
 * (game[k] is matrix k, game_count of them). A joint whose game matrix is
 * missing or out of range gets the zero matrix; sl_chr_derive has already
 * refused an item that binds a vertex to one. */
void sl_chr_joint_matrices(const struct sl_chr_map *map,
                           const float (*game)[16], int game_count,
                           float (*out)[16]);

/* Every vertex of `a` into eye space (3 floats a vertex, into eye), through
 * the per-joint matrices. */
void sl_chr_skin_vertices(const struct sl_wa_asset *a, const float (*joint)[16],
                          float *eye);

/* A VIEW MODEL'S REFLECTION (class GUN, a material naming a reflection
 * image). The item carries no normals, so each vertex's direction is taken
 * from the item's own triangles - the area-weighted sum of the faces that
 * use it, unit length (3 floats a vertex, into item_n; a vertex no face uses
 * gets 0,0,1). The draw then turns those by the same joint matrices as the
 * positions and reads the reflection image where the direction points: the
 * sphere coordinate (0.5 + 0.5 n.x, 0.5 - 0.5 n.y) of the unit eye-space
 * direction (2 floats a vertex, into uv). */
void sl_vm_item_normals(const struct sl_wa_asset *a, float *item_n);
void sl_vm_sphere_coords(const struct sl_wa_asset *a, const float (*joint)[16],
                         const float *item_n, float *uv);

/* THE DYNAMIC DOOR (src/sl_chr_asset.h): the item `a`, related to the door's
 * model by `map` (one rigid joint), cut at the MODEL-space half-space `cut`.
 * Writes the cut item's vertices (ov, and their skin into osk), its indices
 * (oi) and its meshes (om, one per mesh of `a`, same materials and flags, in
 * the same order); *nv / *ni receive the counts. A triangle wholly kept is
 * kept as it is; one across the face is clipped, every attribute
 * interpolated along its edges (Sutherland-Hodgman, one plane); one wholly
 * past the face is laid onto it along the cut axis - the game's own clamp -
 * and dropped when that leaves it no area. Capacities: vcap vertices of at
 * least a->vertex_count + a->index_count, icap indices of at least
 * 2 * a->index_count (sl_door_item_fits). 1, or 0 when the item or the map
 * is not one this can cut or a capacity is short. Pure. */
int sl_door_cut_mesh(const struct sl_wa_asset *a, const struct sl_chr_map *map,
                     const struct sl_door_cut *cut,
                     struct sl_wa_vertex *ov, struct sl_wa_skin *osk, unsigned vcap,
                     unsigned *oi, unsigned icap, struct sl_wa_mesh *om,
                     unsigned *nv, unsigned *ni);

#endif /* SL_GFX_CHARSKIN_H */
