/**
 * sl_gfx_texprov.h - the TEXTURE PROVIDER (#47): which artwork the native
 * renderer uploads for a game texture - ORIGINAL (the ROM's own, decoded),
 * COMMUNITY HD or XBLA - and the one seam that answers it.
 *
 * THE CONTRACT, in one sentence: the renderer asks "is there a replacement
 * for the image at this decoded pointer, at this logical size?" and gets
 * either NULL (draw the decode, exactly as before) or a physical RGBA8 image
 * to upload IN PLACE of the decode - same logical identity, same wrap /
 * clamp / mirror, same tile dimensions, same texture coordinates, only the
 * texels differ. Nothing about the game, the display list or the RDP state
 * moves; the substitution is at glTexImage2D and nowhere else.
 *
 * IDENTITY. GoldenEye's own texLoad (src/game/image.c) is the single point
 * where a texture number becomes decoded bytes in a texture pool, and every
 * display list that samples the image names those bytes by address (the FD
 * settextureimage word is osVirtualToPhysical(tex->data)). So the identity
 * bridge is a native side table, decoded pointer -> N64 texture number,
 * fed by texLoad's native arm (sl_texprov_note_load) and emptied per pool by
 * texInitPool's (sl_texprov_note_pool). No runtime pixel hashing, no
 * filename matching: the id already reaches the seam.
 *
 * THE PACK ROOT. Never a compiled-in path:
 *
 *   1. $SL_TEXPACK_ROOT, if set and non-empty (tests, owner-local runs)
 *   2. <asset override dir>\texpacks - the player-data ladder every other
 *      native asset already uses (%LOCALAPPDATA%\sightline\assets\texpacks)
 *
 * under which each set is a folder of one file per N64 texture number:
 *
 *   <root>\community\<hex4>.sltx        the Community HD set
 *   <root>\xbla\<hex4>.sltx             the user-supplied XBLA set
 *
 * ORIGINAL never touches the root. A set that is not installed is simply
 * every id missing: the game is playable, every texture is ORIGINAL's, and
 * ONE summary line says so.
 *
 * FALLBACK is per texture and never crosses sets: COMMUNITY HD misses ->
 * ORIGINAL, XBLA misses -> ORIGINAL. No set is assumed complete.
 *
 * THE RUNTIME IMAGE FORMAT ("SLTX"). The tree links no PNG decoder and none
 * is added (project rules: no dependency without asking). The converter in
 * the owner's private texture workspace writes each pack image once into
 * this trivially parsed, deterministic form; the runtime reads only this.
 * Little-endian, 28-byte header, tightly packed RGBA8 rows top row first -
 * the orientation sl_tex_decode produces and glTexImage2D takes:
 *
 *   +0   'S' 'L' 'T' 'X'
 *   +4   u32 version           SL_TEXPROV_VERSION
 *   +8   u32 id                the N64 texture number (must equal the
 *                              filename's, and the pool entry's)
 *   +12  u16 n64_w, u16 n64_h  the N64 image's size (must equal the tile's)
 *   +16  u16 phys_w, u16 phys_h  the replacement's size, 1..SL_TEXPROV_MAX_DIM
 *   +20  u32 flags             0 (reserved; non-zero is rejected)
 *   +24  u32 payload           phys_w * phys_h * 4, exactly the bytes that
 *                              follow, exactly to the end of the file
 *
 * Every field is checked before a byte of payload is read or an allocation
 * sized; a malformed file is INVALID (counted, reported once, ORIGINAL drawn)
 * and never a fault.
 *
 * HOST-CLEAN: plain C types only, no game or GL headers, so src/gfx (host
 * headers) compiles it and the self-test (-DSL_TEXPROV_SELFTEST) links it
 * alone. src/game never includes this: image.c's two islands declare the
 * two note functions locally inside their own #ifndef __sgi.
 */
#ifndef SL_GFX_TEXPROV_H
#define SL_GFX_TEXPROV_H

#include <stdio.h>

#define SL_TEXPROV_MAGIC0  'S'
#define SL_TEXPROV_MAGIC1  'L'
#define SL_TEXPROV_MAGIC2  'T'
#define SL_TEXPROV_MAGIC3  'X'
#define SL_TEXPROV_VERSION 1u
#define SL_TEXPROV_HEADER  28u
/* The largest replacement accepted on a side. Equal to the renderer's own
 * decode cap (TEX_MAX_DIM, sl_gfx_dl.c) because the B-119 mip pyramid
 * buffers are sized to it; the owner's pack tops out at 1024. */
#define SL_TEXPROV_MAX_DIM 1024u
/* The largest N64 image the pool can hold on a side (struct tex's width and
 * height are u8). */
#define SL_TEXPROV_MAX_N64 255u
/* Texture numbers are 12 bits in struct tex. */
#define SL_TEXPROV_MAX_ID  4096u

/* A parsed header. */
struct sl_texprov_hdr {
    unsigned id;
    unsigned n64_w, n64_h;
    unsigned phys_w, phys_h;
    unsigned payload;            /* phys_w * phys_h * 4, validated */
};

/* Parse reasons (0 = OK). */
#define SL_TEXPROV_OK          0
#define SL_TEXPROV_E_MAGIC     1
#define SL_TEXPROV_E_VERSION   2
#define SL_TEXPROV_E_ID        3   /* out of range, or not the expected id */
#define SL_TEXPROV_E_N64DIM    4
#define SL_TEXPROV_E_PHYSDIM   5
#define SL_TEXPROV_E_FLAGS     6
#define SL_TEXPROV_E_PAYLOAD   7   /* payload field != w*h*4, or file size */
#define SL_TEXPROV_E_IO        8   /* open / read failed after the header */
#define SL_TEXPROV_E_MEM       9

/* Parse the 28 header bytes. expect_id is the filename's id (the two must
 * agree), or ~0u to accept any id. Never reads past 28 bytes. */
int sl_texprov_parse_header(const unsigned char *hdr, unsigned expect_id,
                            struct sl_texprov_hdr *out);
const char *sl_texprov_strerror(int rc);

/* A resident replacement. rgba is phys_w * phys_h * 4 bytes, owned by the
 * provider's cache; valid until the next lookup (an eviction may free it),
 * which is longer than the renderer holds it (it uploads immediately). */
struct sl_texprov_image {
    unsigned id;
    unsigned n64_w, n64_h;
    unsigned phys_w, phys_h;
    const unsigned char *rgba;
};

/* ---- the identity side table (fed by src/game/image.c's native arms) --- */

/* texLoad inflated `bytes` bytes of texture `texnum` (the pool's struct tex
 * says w x h for its base level) at `data`. Re-registering an address
 * replaces the earlier entry. */
void sl_texprov_note_load(const void *data, unsigned bytes, int texnum,
                          int w, int h);

/* texInitPool: everything registered inside [start, start + bytes) is
 * stale from now on. */
void sl_texprov_note_pool(const void *start, unsigned bytes);

/* ---- the policy ----------------------------------------------------------
 *
 * THE ONE OWNER OF THE DECISION. Reads the settings store's TEXTURES row
 * (ORIGINAL when the store is inactive, missing, malformed or out of range)
 * and answers SL_TEXTURES_*. A change from the last answer bumps the
 * generation the renderer keys its GL cache on, so the switch takes effect at
 * each texture's next resolve. */
int      sl_texprov_active(void);
unsigned sl_texprov_generation(void);

/* The lookup. src is the decoded pointer a tile resolved to, w x h the tile's
 * LOGICAL size (the size the renderer normalises texture coordinates by).
 * NULL = draw the decode: ORIGINAL in force, an unregistered pointer (a mip
 * level inside an image, a game-built image, a raw ROM segment), a tile
 * whose shape is not the whole image, a set that has no file for the id, or
 * a file that failed validation. Never NULL under ORIGINAL. */
const struct sl_texprov_image *sl_texprov_lookup(const void *src,
                                                 unsigned w, unsigned h);

/* The resolved root (for the summary line). Never NULL. */
const char *sl_texprov_root(void);

/* DIAGNOSTIC: the N64 texture number registered for a decoded pointer, and
 * the logical size recorded with it; -1 when the pointer is not a registered
 * decode. Reads the same side table the resolve path reads and changes
 * nothing. Used by the renderer's id-keyed decode dump (SL_TEX_DUMP_IDS). */
int sl_texprov_id_of(const void *src, unsigned *w, unsigned *h);

/* The census line: provider, root, loaded / missing / invalid / resident,
 * the lookup counters. */
void sl_texprov_report(FILE *f);

#endif /* SL_GFX_TEXPROV_H */
