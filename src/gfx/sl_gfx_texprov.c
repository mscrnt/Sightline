/**
 * sl_gfx_texprov.c - the texture provider (#47). See sl_gfx_texprov.h for
 * the contract, the pack root and the on-disk format.
 *
 * FOUR PARTS, each small:
 *
 *   1. the identity side table: decoded pointer -> (id, w, h), an open-
 *      addressed hash of what texLoad inflated, emptied per pool at
 *      texInitPool;
 *   2. the policy: the settings store's TEXTURES row read at every
 *      resolution (one function, one owner), with a generation counter the
 *      renderer folds into its GL cache key so a switch invalidates exactly
 *      the entries that would change;
 *   3. the loader: SLTX header parse, bounded, then the payload read of the
 *      validated length - the one place a file is opened;
 *   4. the resident cache: (set, id) -> LOADED image / MISSING / INVALID,
 *      lazily filled at first use, negative outcomes remembered so a set
 *      that lacks an id costs one stat per run, LRU-evicted under a byte
 *      budget so switching back and forth cannot grow without bound.
 *
 * NO PER-FRAME DISK I/O: the renderer's own GL cache answers repeat draws,
 * and the resident cache answers the renderer's cache misses; the file is
 * read once per (set, id) per residency.
 *
 * NO SIMULATION READS ANYTHING HERE. The side table is written by the game's
 * loader and read by the renderer; no value flows back.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "sl_gfx_texprov.h"
#include "../platform/sl_settings.h"

/* The player-data ladder's directory (src/native/sl_asset_override.c). The
 * self-test links this file alone and supplies its own definition. */
const char *sl_asset_override_dir(void);

/* ------------------------------------------------------ the side table --- */

#define REG_CAP 4096u                 /* pools hold a few hundred at once */

struct reg_ent {
    const void *data;                 /* NULL = empty */
    unsigned    bytes;
    unsigned    id;
    unsigned    w, h;
};
static struct reg_ent g_reg[REG_CAP];
static unsigned g_reg_n, g_reg_full, g_reg_replaced, g_reg_dropped;

static unsigned reg_hash(const void *p)
{
    uintptr_t v = (uintptr_t) p;
    v ^= v >> 17;
    v *= 0x9E3779B1u;
    v ^= v >> 13;
    return (unsigned) v & (REG_CAP - 1u);
}

static struct reg_ent *reg_find(const void *p)
{
    unsigned i = reg_hash(p), n;
    for (n = 0; n < REG_CAP; n++) {
        struct reg_ent *e = &g_reg[i];
        if (e->data == NULL) return NULL;
        if (e->data == p) return e;
        i = (i + 1u) & (REG_CAP - 1u);
    }
    return NULL;
}

static int reg_insert(const struct reg_ent *src)
{
    unsigned i = reg_hash(src->data), n;
    for (n = 0; n < REG_CAP; n++) {
        struct reg_ent *e = &g_reg[i];
        if (e->data == NULL) { *e = *src; g_reg_n++; return 1; }
        if (e->data == src->data) { *e = *src; g_reg_replaced++; return 1; }
        i = (i + 1u) & (REG_CAP - 1u);
    }
    return 0;
}

/* Deletion from an open-addressed table is a rebuild without the dropped
 * entries: it happens once per pool initialisation, never on a draw. */
static void reg_rebuild_without(const unsigned char *lo, const unsigned char *hi)
{
    static struct reg_ent keep[REG_CAP];
    unsigned i, k = 0;
    for (i = 0; i < REG_CAP; i++) {
        const struct reg_ent *e = &g_reg[i];
        const unsigned char *p;
        if (e->data == NULL) continue;
        p = (const unsigned char *) e->data;
        if (p >= lo && p < hi) { g_reg_dropped++; continue; }
        keep[k++] = *e;
    }
    memset(g_reg, 0, sizeof g_reg);
    g_reg_n = 0;
    for (i = 0; i < k; i++) reg_insert(&keep[i]);
}

void sl_texprov_note_load(const void *data, unsigned bytes, int texnum,
                          int w, int h)
{
    struct reg_ent e;
    if (data == NULL || bytes == 0u) return;
    if (texnum < 0 || (unsigned) texnum >= SL_TEXPROV_MAX_ID) return;
    if (w <= 0 || h <= 0 || (unsigned) w > SL_TEXPROV_MAX_N64
        || (unsigned) h > SL_TEXPROV_MAX_N64) return;
    /* The table is bounded; if it is ever full the texture is simply
     * ORIGINAL's (counted), never a fault. Three quarters is the load
     * factor at which linear probing stays cheap. */
    if (g_reg_n >= (REG_CAP * 3u) / 4u && reg_find(data) == NULL) {
        g_reg_full++;
        return;
    }
    e.data = data; e.bytes = bytes; e.id = (unsigned) texnum;
    e.w = (unsigned) w; e.h = (unsigned) h;
    reg_insert(&e);
}

void sl_texprov_note_pool(const void *start, unsigned bytes)
{
    const unsigned char *lo = (const unsigned char *) start;
    if (start == NULL || bytes == 0u || g_reg_n == 0u) return;
    reg_rebuild_without(lo, lo + bytes);
}

/* ------------------------------------------------------------ the policy -- */

static int      g_last_set = -1;
static unsigned g_gen;                 /* 0 until the first non-ORIGINAL ask */
static unsigned g_switches;

#ifdef SL_TEXPROV_SELFTEST
static int g_test_setting;
static int store_setting(void) { return g_test_setting; }
#else
static int store_setting(void) { return sl_settings_get(SL_SET_TEXTURES); }
#endif

int sl_texprov_active(void)
{
    int v = store_setting();
    if (v < 0 || v >= SL_TEXTURES_COUNT) v = SL_TEXTURES_ORIGINAL;
    if (v != g_last_set) {
        if (g_last_set >= 0) g_switches++;
        g_last_set = v;
        g_gen++;
    }
    return v;
}

unsigned sl_texprov_generation(void) { return g_gen; }

/* -------------------------------------------------------------- the root -- */

static char g_root[512];
static int  g_root_done;
static int  g_root_from_env;

static void path_join(char *out, unsigned cap, const char *a, const char *b)
{
    unsigned n;
    out[0] = '\0';
    if (a == NULL) a = "";
    n = (unsigned) strlen(a);
    if (n + 1u >= cap) return;
    memcpy(out, a, n);
    if (n != 0u && out[n - 1u] != '\\' && out[n - 1u] != '/') out[n++] = '\\';
    if (n + (unsigned) strlen(b) + 1u > cap) { out[0] = '\0'; return; }
    strcpy(out + n, b);
}

/* The identity the registry already holds, read back for DIAGNOSTICS: which
 * N64 texture number the decode at this pointer is, and the logical size the
 * pool recorded for it. The renderer's id-keyed decode dump (SL_TEX_DUMP_IDS,
 * sl_gfx_dl.c) is the only caller; the resolve path uses reg_find directly
 * and is unchanged. -1 = not a registered decode (a mip level, a pool that
 * has been emptied, an image that never passed through texLoad). */
int sl_texprov_id_of(const void *src, unsigned *w, unsigned *h)
{
    const struct reg_ent *r = reg_find(src);
    if (r == NULL) return -1;
    if (w != NULL) *w = r->w;
    if (h != NULL) *h = r->h;
    return (int) r->id;
}

const char *sl_texprov_root(void)
{
    const char *e;
    if (g_root_done) return g_root;
    g_root_done = 1;
    e = getenv("SL_TEXPACK_ROOT");
    if (e != NULL && e[0] != '\0' && strlen(e) + 1u < sizeof g_root) {
        strcpy(g_root, e);
        g_root_from_env = 1;
        return g_root;
    }
    path_join(g_root, sizeof g_root, sl_asset_override_dir(), "texpacks");
    if (g_root[0] == '\0') strcpy(g_root, ".");
    return g_root;
}

static const char *set_dirname(int set)
{
    switch (set) {
    case SL_TEXTURES_COMMUNITY: return "community";
    case SL_TEXTURES_XBLA:      return "xbla";
    default:                    return NULL;
    }
}

/* ------------------------------------------------------------- the parse -- */

static unsigned rd16(const unsigned char *p)
{
    return (unsigned) p[0] | ((unsigned) p[1] << 8);
}
static unsigned rd32(const unsigned char *p)
{
    return (unsigned) p[0] | ((unsigned) p[1] << 8)
         | ((unsigned) p[2] << 16) | ((unsigned) p[3] << 24);
}

int sl_texprov_parse_header(const unsigned char *hdr, unsigned expect_id,
                            struct sl_texprov_hdr *out)
{
    struct sl_texprov_hdr h;
    unsigned long need;

    if (hdr == NULL || out == NULL) return SL_TEXPROV_E_IO;
    if (hdr[0] != SL_TEXPROV_MAGIC0 || hdr[1] != SL_TEXPROV_MAGIC1
        || hdr[2] != SL_TEXPROV_MAGIC2 || hdr[3] != SL_TEXPROV_MAGIC3)
        return SL_TEXPROV_E_MAGIC;
    if (rd32(hdr + 4) != SL_TEXPROV_VERSION) return SL_TEXPROV_E_VERSION;
    h.id = rd32(hdr + 8);
    if (h.id >= SL_TEXPROV_MAX_ID) return SL_TEXPROV_E_ID;
    if (expect_id != ~0u && h.id != expect_id) return SL_TEXPROV_E_ID;
    h.n64_w = rd16(hdr + 12); h.n64_h = rd16(hdr + 14);
    if (h.n64_w == 0u || h.n64_h == 0u || h.n64_w > SL_TEXPROV_MAX_N64
        || h.n64_h > SL_TEXPROV_MAX_N64)
        return SL_TEXPROV_E_N64DIM;
    h.phys_w = rd16(hdr + 16); h.phys_h = rd16(hdr + 18);
    if (h.phys_w == 0u || h.phys_h == 0u || h.phys_w > SL_TEXPROV_MAX_DIM
        || h.phys_h > SL_TEXPROV_MAX_DIM)
        return SL_TEXPROV_E_PHYSDIM;
    if (rd32(hdr + 20) != 0u) return SL_TEXPROV_E_FLAGS;
    /* Both factors are bounded above (<= 1024) before this multiply, so it
     * cannot overflow; the file's own claim must equal it exactly. */
    need = (unsigned long) h.phys_w * (unsigned long) h.phys_h * 4ul;
    if ((unsigned long) rd32(hdr + 24) != need) return SL_TEXPROV_E_PAYLOAD;
    h.payload = (unsigned) need;
    *out = h;
    return SL_TEXPROV_OK;
}

const char *sl_texprov_strerror(int rc)
{
    switch (rc) {
    case SL_TEXPROV_OK:        return "ok";
    case SL_TEXPROV_E_MAGIC:   return "not an SLTX file";
    case SL_TEXPROV_E_VERSION: return "unsupported SLTX version";
    case SL_TEXPROV_E_ID:      return "texture id out of range or not the file's";
    case SL_TEXPROV_E_N64DIM:  return "N64 dimensions out of range";
    case SL_TEXPROV_E_PHYSDIM: return "replacement dimensions out of range";
    case SL_TEXPROV_E_FLAGS:   return "reserved flags set";
    case SL_TEXPROV_E_PAYLOAD: return "payload size disagrees with the dimensions or the file";
    case SL_TEXPROV_E_IO:      return "read failed";
    case SL_TEXPROV_E_MEM:     return "out of memory";
    default:                   return "unknown";
    }
}

/* Load one file. Returns SL_TEXPROV_OK with *rgba malloc'd (caller frees),
 * SL_TEXPROV_E_IO when the file does not exist or cannot be read (a MISS),
 * or another reason (INVALID). The header is read and validated FIRST; the
 * payload allocation is sized by the validated dimensions, never by the
 * file, and the file must end exactly where the payload does. */
static int load_file(const char *path, unsigned expect_id,
                     struct sl_texprov_hdr *hdr, unsigned char **rgba,
                     int *existed)
{
    unsigned char head[SL_TEXPROV_HEADER];
    FILE *f;
    unsigned char *buf;
    long size;
    int rc;

    *rgba = NULL;
    *existed = 0;
    f = fopen(path, "rb");
    if (f == NULL) return SL_TEXPROV_E_IO;
    *existed = 1;
    if (fread(head, 1, SL_TEXPROV_HEADER, f) != SL_TEXPROV_HEADER) {
        fclose(f);
        return SL_TEXPROV_E_PAYLOAD;              /* shorter than a header */
    }
    rc = sl_texprov_parse_header(head, expect_id, hdr);
    if (rc != SL_TEXPROV_OK) { fclose(f); return rc; }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return SL_TEXPROV_E_IO; }
    size = ftell(f);
    if (size < 0 || (unsigned long) size != (unsigned long) SL_TEXPROV_HEADER
                                             + (unsigned long) hdr->payload) {
        fclose(f);
        return SL_TEXPROV_E_PAYLOAD;              /* truncated or padded */
    }
    if (fseek(f, (long) SL_TEXPROV_HEADER, SEEK_SET) != 0) {
        fclose(f); return SL_TEXPROV_E_IO;
    }
    buf = (unsigned char *) malloc((size_t) hdr->payload);
    if (buf == NULL) { fclose(f); return SL_TEXPROV_E_MEM; }
    if (fread(buf, 1, (size_t) hdr->payload, f) != (size_t) hdr->payload) {
        free(buf); fclose(f); return SL_TEXPROV_E_IO;
    }
    fclose(f);
    *rgba = buf;
    return SL_TEXPROV_OK;
}

/* --------------------------------------------------- the resident cache --- */

#define IMG_CAP 8192u                  /* 3 sets x 2698 ids fits at <75% */
#define IMG_EMPTY   0
#define IMG_LOADED  1
#define IMG_MISSING 2
#define IMG_INVALID 3

struct img_ent {
    unsigned      key;                 /* (set << 16) | id, +1 so 0 is empty */
    unsigned char state;
    unsigned char rc;                  /* the INVALID reason */
    unsigned      last_use;
    struct sl_texprov_image img;       /* rgba NULL unless LOADED */
    unsigned      bytes;
};
static struct img_ent g_img[IMG_CAP];
static unsigned g_img_n;                     /* entries in use (any state) */
static unsigned g_res_n;                     /* LOADED with pixels resident */
static unsigned long g_res_bytes;
static unsigned g_use_clock;
static unsigned g_loaded, g_missing, g_invalid, g_evicted, g_reloaded;
static unsigned g_lookups, g_unregistered, g_shape_miss, g_hits, g_misses_seen;
static unsigned g_root_missing[SL_TEXTURES_COUNT];   /* the set's folder */
static unsigned g_root_checked[SL_TEXTURES_COUNT];

static unsigned long budget_bytes(void)
{
    static unsigned long b;
    if (b == 0ul) {
        const char *v = getenv("SL_TEXPACK_BUDGET_MB");
        unsigned long mb = (v != NULL && *v != '\0') ? strtoul(v, NULL, 10) : 256ul;
        if (mb < 8ul) mb = 8ul;
        if (mb > 2048ul) mb = 2048ul;
        b = mb * 1024ul * 1024ul;
    }
    return b;
}

static unsigned img_key(int set, unsigned id)
{
    return (((unsigned) set << 16) | (id & 0xFFFFu)) + 1u;
}

static unsigned img_hash(unsigned key)
{
    key ^= key >> 16; key *= 0x7FEB352Du; key ^= key >> 15;
    return key & (IMG_CAP - 1u);
}

static struct img_ent *img_find(unsigned key, int create)
{
    unsigned i = img_hash(key), n;
    for (n = 0; n < IMG_CAP; n++) {
        struct img_ent *e = &g_img[i];
        if (e->key == key) return e;
        if (e->key == 0u) {
            if (!create) return NULL;
            if (g_img_n >= (IMG_CAP * 3u) / 4u) return NULL;
            memset(e, 0, sizeof *e);
            e->key = key;
            g_img_n++;
            return e;
        }
        i = (i + 1u) & (IMG_CAP - 1u);
    }
    return NULL;
}

static void img_drop_pixels(struct img_ent *e)
{
    if (e->state != IMG_LOADED || e->img.rgba == NULL) return;
    free((void *) e->img.rgba);
    e->img.rgba = NULL;
    g_res_bytes -= e->bytes;
    g_res_n--;
    /* The entry keeps its metadata and returns to EMPTY-state semantics:
     * the next lookup re-reads the file. */
    e->state = IMG_EMPTY;
    e->bytes = 0u;
}

/* Make room for `need` bytes: evict least-recently used resident images
 * until the budget holds. Never evicts the entry being filled (it is not
 * LOADED yet). */
static void img_make_room(unsigned long need)
{
    while (g_res_n > 0u && g_res_bytes + need > budget_bytes()) {
        unsigned i, best = IMG_CAP, best_use = 0u;
        for (i = 0; i < IMG_CAP; i++) {
            const struct img_ent *e = &g_img[i];
            if (e->state != IMG_LOADED || e->img.rgba == NULL) continue;
            if (best == IMG_CAP || e->last_use < best_use) {
                best = i; best_use = e->last_use;
            }
        }
        if (best == IMG_CAP) break;
        img_drop_pixels(&g_img[best]);
        g_evicted++;
    }
}

/* ---------------------------------------------------------- diagnostics --- */

static int dbg_on(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_TEX_PROVIDER_DBG");
                  on = (v != NULL && *v != '\0' && *v != '0'); }
    return on;
}

static unsigned watch_id(void)
{
    static unsigned id = 0xFFFFFFFFu;
    if (id == 0xFFFFFFFFu) {
        const char *v = getenv("SL_TEX_PROVIDER_WATCH");
        id = (v != NULL && *v != '\0') ? (unsigned) strtoul(v, NULL, 16) : 0xFFFFFFFEu;
    }
    return id;
}

static unsigned g_dbg_lines, g_watch_lines;
#define DBG_MAX_LINES   4096u
#define WATCH_MAX_LINES 256u

static int g_summary_said[SL_TEXTURES_COUNT];

static void say_summary(int set, const char *dir, int present)
{
    if (g_summary_said[set]) return;
    g_summary_said[set] = 1;
    fprintf(stderr, "sl_texprov: TEXTURES=%s root=%s (%s) set-folder=%s %s\n",
            sl_textures_name(set), sl_texprov_root(),
            g_root_from_env ? "SL_TEXPACK_ROOT" : "player data",
            dir, present ? "found" : "NOT FOUND - every texture falls back to ORIGINAL");
}

/* Does the set's folder exist? One probe per set per run, so an absent pack
 * never costs a file open per texture: the set is then MISSING for every id
 * without touching the disk again. */
static int set_folder_probe(const char *dir)
{
#ifdef _WIN32
    /* _access is <io.h>; declared here to keep the include set host-clean
     * across the two compile classes. */
    extern int _access(const char *, int);
    return _access(dir, 0) == 0;
#else
    extern int access(const char *, int);
    return access(dir, 0) == 0;
#endif
}

/* ---------------------------------------------------------------- lookup -- */

static const struct sl_texprov_image *resolve(int set, const struct reg_ent *r,
                                              unsigned w, unsigned h)
{
    unsigned key = img_key(set, r->id);
    struct img_ent *e = img_find(key, 1);
    char dir[560], path[600], name[16];
    const char *sub = set_dirname(set);

    if (e == NULL || sub == NULL) return NULL;
    if (e->state == IMG_LOADED) {
        e->last_use = ++g_use_clock;
        g_hits++;
        return &e->img;
    }
    if (e->state == IMG_MISSING || e->state == IMG_INVALID) {
        g_misses_seen++;
        return NULL;
    }

    /* EMPTY: first ask for this (set, id) - or a re-ask after eviction. */
    path_join(dir, sizeof dir, sl_texprov_root(), sub);
    if (!g_root_checked[set]) {
        g_root_checked[set] = 1;
        g_root_missing[set] = !set_folder_probe(dir);
        say_summary(set, dir, !g_root_missing[set]);
    }
    if (g_root_missing[set]) {
        e->state = IMG_MISSING;
        g_missing++;
        g_misses_seen++;
        return NULL;
    }
    sprintf(name, "%04x.sltx", r->id & 0xFFFFu);
    path_join(path, sizeof path, dir, name);
    {
        struct sl_texprov_hdr hdr;
        unsigned char *rgba = NULL;
        int existed = 0;
        int rc = load_file(path, r->id, &hdr, &rgba, &existed);
        if (rc == SL_TEXPROV_E_IO && !existed) {
            e->state = IMG_MISSING;
            g_missing++;
            g_misses_seen++;
            if (dbg_on() && g_dbg_lines < DBG_MAX_LINES) {
                g_dbg_lines++;
                fprintf(stderr, "sl_texprov: id=%04x %s MISSING n64=%ux%u\n",
                        r->id, sub, r->w, r->h);
            }
            return NULL;
        }
        if (rc != SL_TEXPROV_OK) {
            e->state = IMG_INVALID;
            e->rc = (unsigned char) rc;
            g_invalid++;
            g_misses_seen++;
            fprintf(stderr, "sl_texprov: id=%04x %s INVALID (%s) - ORIGINAL drawn: %s\n",
                    r->id, sub, sl_texprov_strerror(rc), path);
            return NULL;
        }
        /* The file's N64 size must be the pool entry's: a replacement for a
         * differently shaped image would be sampled at the wrong scale. */
        if (hdr.n64_w != r->w || hdr.n64_h != r->h) {
            free(rgba);
            e->state = IMG_INVALID;
            e->rc = SL_TEXPROV_E_N64DIM;
            g_invalid++;
            g_misses_seen++;
            fprintf(stderr, "sl_texprov: id=%04x %s INVALID (file says N64 %ux%u,"
                            " the pool holds %ux%u) - ORIGINAL drawn: %s\n",
                    r->id, sub, hdr.n64_w, hdr.n64_h, r->w, r->h, path);
            return NULL;
        }
        img_make_room((unsigned long) hdr.payload);
        e->state = IMG_LOADED;
        e->img.id = hdr.id;
        e->img.n64_w = hdr.n64_w; e->img.n64_h = hdr.n64_h;
        e->img.phys_w = hdr.phys_w; e->img.phys_h = hdr.phys_h;
        e->img.rgba = rgba;
        e->bytes = hdr.payload;
        e->last_use = ++g_use_clock;
        g_res_n++;
        g_res_bytes += hdr.payload;
        if (e->rc == 0xFFu) g_reloaded++;
        e->rc = 0xFFu;                       /* has been loaded at least once */
        g_loaded++;
        g_hits++;
        if (dbg_on() && g_dbg_lines < DBG_MAX_LINES) {
            g_dbg_lines++;
            fprintf(stderr, "sl_texprov: id=%04x %s LOADED n64=%ux%u phys=%ux%u"
                            " (%u bytes; resident %u images %lu KB)\n",
                    r->id, sub, hdr.n64_w, hdr.n64_h, hdr.phys_w, hdr.phys_h,
                    hdr.payload, g_res_n, g_res_bytes / 1024ul);
        }
        (void) w; (void) h;
        return &e->img;
    }
}

/* SL_TEX_PROVIDER_WATCH: one line per CHANGE of the watched id's answer (the
 * id resolves once per triangle, so a per-lookup line would spend the budget
 * in a frame) - the set, the generation, the tile and image sizes, and what
 * was answered - so a live switch reads as a short transcript. */
static void watch_note(const struct reg_ent *r, int set, unsigned w, unsigned h,
                       const struct sl_texprov_image *img)
{
    static unsigned last_gen = 0xFFFFFFFFu; static int last_rep = -1;
    int rep = img != NULL;
    if (r == NULL || r->id != watch_id() || g_watch_lines >= WATCH_MAX_LINES) return;
    if (last_gen == g_gen && last_rep == rep) return;
    last_gen = g_gen; last_rep = rep;
    g_watch_lines++;
    fprintf(stderr, "sl_texprov: WATCH id=%04x set=%s gen=%u tile=%ux%u image=%ux%u -> %s%s%ux%u\n",
            r->id, sl_textures_name(set), g_gen, w, h, r->w, r->h,
            img ? "REPLACED phys=" : "ORIGINAL",
            img ? "" : " ", img ? img->phys_w : 0u, img ? img->phys_h : 0u);
}

const struct sl_texprov_image *sl_texprov_lookup(const void *src,
                                                 unsigned w, unsigned h)
{
    int set = sl_texprov_active();
    const struct reg_ent *r;
    const struct sl_texprov_image *img;

    if (set == SL_TEXTURES_ORIGINAL) {
        /* ORIGINAL touches nothing - except that a WATCHED id still reports
         * the transition, so a live switch back reads in the transcript. */
        if (watch_id() < SL_TEXPROV_MAX_ID) watch_note(reg_find(src), set, w, h, NULL);
        return NULL;
    }
    g_lookups++;
    r = reg_find(src);
    if (r == NULL) {
        g_unregistered++;
        if (dbg_on() && g_dbg_lines < DBG_MAX_LINES) {
            /* Which sources the bridge cannot name, once each: a mip level
             * inside a registered image, a game-built image (the barrel
             * rows), a raw ROM segment (the Rareware maps), a font. */
            static const void *seen[48]; static unsigned nseen;
            unsigned i;
            for (i = 0; i < nseen; i++) if (seen[i] == src) break;
            if (i == nseen && nseen < 48u) {
                seen[nseen++] = src;
                g_dbg_lines++;
                fprintf(stderr, "sl_texprov: src=%p UNREGISTERED tile %ux%u"
                                " - ORIGINAL drawn\n", src, w, h);
            }
        }
        return NULL;
    }
    if (r->w != w || r->h != h) {
        g_shape_miss++;
        if (dbg_on() && g_dbg_lines < DBG_MAX_LINES) {
            static unsigned seen[64][3]; static unsigned nseen;
            unsigned i;
            for (i = 0; i < nseen; i++)
                if (seen[i][0] == r->id && seen[i][1] == w && seen[i][2] == h) break;
            if (i == nseen && nseen < 64u) {
                seen[nseen][0] = r->id; seen[nseen][1] = w; seen[nseen][2] = h; nseen++;
                g_dbg_lines++;
                fprintf(stderr, "sl_texprov: id=%04x SHAPE-MISS tile %ux%u vs image %ux%u"
                                " - ORIGINAL drawn\n", r->id, w, h, r->w, r->h);
            }
        }
        return NULL;
    }
    img = resolve(set, r, w, h);
    watch_note(r, set, w, h, img);
    return img;
}

void sl_texprov_report(FILE *f)
{
    int set = (g_last_set >= 0) ? g_last_set : SL_TEXTURES_ORIGINAL;
    if (f == NULL) return;
    fprintf(f, "sl_texprov: TEXTURES=%s gen=%u switches=%u  registry=%u"
               " (replaced=%u dropped=%u full=%u)  lookups=%u unregistered=%u"
               " shape-miss=%u hits=%u miss-hits=%u  files loaded=%u"
               " (reloaded=%u) missing=%u invalid=%u  resident=%u images"
               " %lu KB evicted=%u budget=%lu MB\n",
            sl_textures_name(set), g_gen, g_switches, g_reg_n,
            g_reg_replaced, g_reg_dropped, g_reg_full,
            g_lookups, g_unregistered, g_shape_miss, g_hits, g_misses_seen,
            g_loaded, g_reloaded, g_missing, g_invalid,
            g_res_n, g_res_bytes / 1024ul, g_evicted,
            budget_bytes() / (1024ul * 1024ul));
}

/* ================================================================ selftest */
#ifdef SL_TEXPROV_SELFTEST
/*
 * tools/windows/texprovtest.ps1 compiles this file alone with
 * -DSL_TEXPROV_SELFTEST. Every fixture is SYNTHETIC - small asymmetric RGBA
 * patterns written by the test itself under %TEMP% - and the root is pointed
 * at them through SL_TEXPACK_ROOT. No pack, no ROM, no asset is read.
 */
#include <direct.h>

const char *sl_asset_override_dir(void) { return "."; }

static int g_checks, g_fail;
static void ck(int ok, const char *what)
{
    g_checks++;
    if (!ok) { g_fail++; printf("  FAIL: %s\n", what); }
}

static void wr16(unsigned char *p, unsigned v) { p[0] = (unsigned char) v; p[1] = (unsigned char) (v >> 8); }
static void wr32(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char) v; p[1] = (unsigned char) (v >> 8);
    p[2] = (unsigned char) (v >> 16); p[3] = (unsigned char) (v >> 24);
}

/* A synthetic header, every field caller-chosen. */
static void mk_hdr(unsigned char *h, const char *magic, unsigned ver, unsigned id,
                   unsigned nw, unsigned nh, unsigned pw, unsigned ph,
                   unsigned flags, unsigned payload)
{
    memcpy(h, magic, 4);
    wr32(h + 4, ver); wr32(h + 8, id);
    wr16(h + 12, nw); wr16(h + 14, nh);
    wr16(h + 16, pw); wr16(h + 18, ph);
    wr32(h + 20, flags); wr32(h + 24, payload);
}

/* An asymmetric pw x ph pattern: pixel (x, y) = (x*40, y*70, x^y, 255 or
 * the cutout alpha), so a flip in either axis changes the bytes. */
static void mk_pixels(unsigned char *p, unsigned pw, unsigned ph, int cutout)
{
    unsigned x, y;
    for (y = 0; y < ph; y++)
        for (x = 0; x < pw; x++) {
            unsigned char *q = p + (y * pw + x) * 4u;
            q[0] = (unsigned char) (x * 40u); q[1] = (unsigned char) (y * 70u);
            q[2] = (unsigned char) (x ^ y);
            q[3] = (unsigned char) (cutout && x == 0u ? 0u : 255u);
        }
}

static int write_file(const char *path, const unsigned char *hdr,
                      const unsigned char *px, unsigned pxbytes, int truncate_by)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) return 0;
    fwrite(hdr, 1, SL_TEXPROV_HEADER, f);
    if (px != NULL && (int) pxbytes - truncate_by > 0)
        fwrite(px, 1, (size_t) ((int) pxbytes - truncate_by), f);
    fclose(f);
    return 1;
}

int main(void)
{
    char root[400], dir_c[440], dir_x[440], path[520];
    unsigned char hdr[SL_TEXPROV_HEADER], px[4 * 2 * 4], big[8 * 4 * 4];
    const char *tmp = getenv("TEMP");
    unsigned char pool_a[64], pool_b[64], pool_c[64];
    struct sl_texprov_hdr h;
    const struct sl_texprov_image *img;

    if (tmp == NULL || *tmp == '\0') tmp = ".";
    sprintf(root, "%s\\sl_texprov_selftest", tmp);
    _mkdir(root);
    sprintf(dir_c, "%s\\community", root); _mkdir(dir_c);
    sprintf(dir_x, "%s\\xbla", root);      _mkdir(dir_x);
    {   static char env[600];
        sprintf(env, "SL_TEXPACK_ROOT=%s", root);
        _putenv(env);
        _putenv("SL_TEXPACK_BUDGET_MB=8");
    }

    /* 1. The parser, on synthetic headers: valid, then each rejection. */
    mk_pixels(px, 4, 2, 0);
    mk_hdr(hdr, "SLTX", 1, 0x12, 2, 1, 4, 2, 0, 32);
    ck(sl_texprov_parse_header(hdr, 0x12, &h) == SL_TEXPROV_OK
       && h.id == 0x12 && h.n64_w == 2 && h.n64_h == 1 && h.phys_w == 4 && h.phys_h == 2 && h.payload == 32,
       "valid header parses: id 0x12, N64 2x1, physical 4x2, payload 32");
    ck(sl_texprov_parse_header(hdr, ~0u, &h) == SL_TEXPROV_OK, "any-id parse accepts it");
    ck(sl_texprov_parse_header(hdr, 0x13, &h) == SL_TEXPROV_E_ID, "wrong expected id -> E_ID");
    mk_hdr(hdr, "SLTY", 1, 0x12, 2, 1, 4, 2, 0, 32);
    ck(sl_texprov_parse_header(hdr, 0x12, &h) == SL_TEXPROV_E_MAGIC, "wrong magic -> E_MAGIC");
    mk_hdr(hdr, "SLTX", 2, 0x12, 2, 1, 4, 2, 0, 32);
    ck(sl_texprov_parse_header(hdr, 0x12, &h) == SL_TEXPROV_E_VERSION, "version 2 -> E_VERSION");
    mk_hdr(hdr, "SLTX", 1, 4096, 2, 1, 4, 2, 0, 32);
    ck(sl_texprov_parse_header(hdr, ~0u, &h) == SL_TEXPROV_E_ID, "id 4096 -> E_ID");
    mk_hdr(hdr, "SLTX", 1, 0x12, 0, 1, 4, 2, 0, 32);
    ck(sl_texprov_parse_header(hdr, 0x12, &h) == SL_TEXPROV_E_N64DIM, "N64 width 0 -> E_N64DIM");
    mk_hdr(hdr, "SLTX", 1, 0x12, 2, 256, 4, 2, 0, 32);
    ck(sl_texprov_parse_header(hdr, 0x12, &h) == SL_TEXPROV_E_N64DIM, "N64 height 256 -> E_N64DIM");
    mk_hdr(hdr, "SLTX", 1, 0x12, 2, 1, 0, 2, 0, 0);
    ck(sl_texprov_parse_header(hdr, 0x12, &h) == SL_TEXPROV_E_PHYSDIM, "physical width 0 -> E_PHYSDIM");
    mk_hdr(hdr, "SLTX", 1, 0x12, 2, 1, 2048, 2, 0, 16384);
    ck(sl_texprov_parse_header(hdr, 0x12, &h) == SL_TEXPROV_E_PHYSDIM, "physical width 2048 -> E_PHYSDIM (oversize)");
    mk_hdr(hdr, "SLTX", 1, 0x12, 2, 1, 65535, 65535, 0, 0xFFFFFFFCu);
    ck(sl_texprov_parse_header(hdr, 0x12, &h) == SL_TEXPROV_E_PHYSDIM, "65535x65535 (an overflowing product) -> E_PHYSDIM before any multiply");
    mk_hdr(hdr, "SLTX", 1, 0x12, 2, 1, 4, 2, 1, 32);
    ck(sl_texprov_parse_header(hdr, 0x12, &h) == SL_TEXPROV_E_FLAGS, "flags 1 -> E_FLAGS");
    mk_hdr(hdr, "SLTX", 1, 0x12, 2, 1, 4, 2, 0, 31);
    ck(sl_texprov_parse_header(hdr, 0x12, &h) == SL_TEXPROV_E_PAYLOAD, "payload 31 for 4x2 -> E_PAYLOAD");
    mk_hdr(hdr, "SLTX", 1, 0x12, 2, 1, 4, 2, 0, 33);
    ck(sl_texprov_parse_header(hdr, 0x12, &h) == SL_TEXPROV_E_PAYLOAD, "payload 33 for 4x2 -> E_PAYLOAD");
    ck(sl_texprov_parse_header(NULL, 0x12, &h) == SL_TEXPROV_E_IO, "NULL header -> E_IO");

    /* 2. The registry. */
    sl_texprov_note_load(pool_a, 64, 0x12, 2, 1);
    sl_texprov_note_load(pool_b, 64, 0x34, 2, 1);
    sl_texprov_note_load(pool_c, 64, 0x56, 2, 1);
    sl_texprov_note_load(NULL, 64, 0x99, 2, 1);      /* ignored */
    sl_texprov_note_load(pool_a + 1, 0, 0x99, 2, 1); /* ignored */
    sl_texprov_note_load(pool_a + 2, 64, 5000, 2, 1); /* id out of range, ignored */
    sl_texprov_note_load(pool_a + 3, 64, 0x99, 300, 1); /* w out of range, ignored */

    /* 3. ORIGINAL never looks anything up (the root is never opened). */
    g_test_setting = SL_TEXTURES_ORIGINAL;
    ck(sl_texprov_active() == SL_TEXTURES_ORIGINAL, "active = ORIGINAL");
    ck(sl_texprov_lookup(pool_a, 2, 1) == NULL, "ORIGINAL: lookup answers NULL");
    ck(sl_texprov_generation() == 1u, "first ask sets generation 1");

    /* 4. COMMUNITY: a valid file for 0x12 (asymmetric 4x2), none for 0x34,
     *    a truncated one for 0x56. */
    mk_hdr(hdr, "SLTX", 1, 0x12, 2, 1, 4, 2, 0, 32);
    sprintf(path, "%s\\0012.sltx", dir_c); ck(write_file(path, hdr, px, 32, 0), "write community 0012");
    mk_hdr(hdr, "SLTX", 1, 0x56, 2, 1, 4, 2, 0, 32);
    sprintf(path, "%s\\0056.sltx", dir_c); ck(write_file(path, hdr, px, 32, 4), "write community 0056 (truncated pixels)");
    g_test_setting = SL_TEXTURES_COMMUNITY;
    ck(sl_texprov_active() == SL_TEXTURES_COMMUNITY && sl_texprov_generation() == 2u,
       "switch to COMMUNITY bumps the generation to 2");
    img = sl_texprov_lookup(pool_a, 2, 1);
    ck(img != NULL && img->id == 0x12 && img->phys_w == 4 && img->phys_h == 2 && img->n64_w == 2 && img->n64_h == 1,
       "COMMUNITY 0x12: replacement 4x2 for the 2x1 image");
    ck(img != NULL && memcmp(img->rgba, px, 32) == 0, "the pixels are the file's, byte for byte (no flip, no reorder)");
    ck(img != NULL && img->rgba[0] == 0 && img->rgba[4] == 40 && img->rgba[4 * 4 + 1] == 70,
       "asymmetric witness: (1,0).r = 40, (0,1).g = 70 - rows top first");
    ck(sl_texprov_lookup(pool_a, 2, 1) == img, "second lookup: the resident image, no re-read");
    ck(sl_texprov_lookup(pool_a, 4, 1) == NULL, "a 4x1 tile over the 2x1 image: SHAPE-MISS -> NULL");
    ck(sl_texprov_lookup(pool_b, 2, 1) == NULL, "COMMUNITY 0x34: no file -> NULL (ORIGINAL)");
    ck(sl_texprov_lookup(pool_b, 2, 1) == NULL, "0x34 again: negatively cached, still NULL");
    ck(sl_texprov_lookup(pool_c, 2, 1) == NULL, "COMMUNITY 0x56: truncated file -> INVALID -> NULL");
    ck(sl_texprov_lookup(pool_c + 8, 2, 1) == NULL, "an unregistered pointer -> NULL");

    /* 5. XBLA: a file for 0x34 only, with the WRONG embedded id for 0x12. */
    mk_hdr(hdr, "SLTX", 1, 0x34, 2, 1, 4, 2, 0, 32);
    sprintf(path, "%s\\0034.sltx", dir_x); ck(write_file(path, hdr, px, 32, 0), "write xbla 0034");
    mk_hdr(hdr, "SLTX", 1, 0x13, 2, 1, 4, 2, 0, 32);
    sprintf(path, "%s\\0012.sltx", dir_x); ck(write_file(path, hdr, px, 32, 0), "write xbla 0012 carrying id 0x13");
    g_test_setting = SL_TEXTURES_XBLA;
    ck(sl_texprov_active() == SL_TEXTURES_XBLA && sl_texprov_generation() == 3u, "switch to XBLA: generation 3");
    ck(sl_texprov_lookup(pool_a, 2, 1) == NULL, "XBLA 0x12: embedded id 0x13 -> INVALID -> NULL (never the COMMUNITY file)");
    img = sl_texprov_lookup(pool_b, 2, 1);
    ck(img != NULL && img->id == 0x34, "XBLA 0x34: replaced (COMMUNITY had none - no cross-set fallback either way)");
    ck(sl_texprov_lookup(pool_c, 2, 1) == NULL, "XBLA 0x56: no file -> NULL");

    /* 6. Back to COMMUNITY: 0x12 replaced again, 0x34 not; then ORIGINAL. */
    g_test_setting = SL_TEXTURES_COMMUNITY;
    ck(sl_texprov_active() == SL_TEXTURES_COMMUNITY && sl_texprov_generation() == 4u, "back to COMMUNITY: generation 4");
    ck(sl_texprov_lookup(pool_a, 2, 1) != NULL && sl_texprov_lookup(pool_b, 2, 1) == NULL,
       "COMMUNITY again: 0x12 replaced, 0x34 ORIGINAL");
    g_test_setting = SL_TEXTURES_ORIGINAL;
    ck(sl_texprov_active() == SL_TEXTURES_ORIGINAL && sl_texprov_generation() == 5u, "ORIGINAL: generation 5");
    ck(sl_texprov_lookup(pool_a, 2, 1) == NULL && sl_texprov_lookup(pool_b, 2, 1) == NULL, "ORIGINAL: nothing replaced");
    g_test_setting = 7;
    ck(sl_texprov_active() == SL_TEXTURES_ORIGINAL, "an out-of-range store value reads ORIGINAL");
    g_test_setting = SL_TEXTURES_COMMUNITY;

    /* 7. A pool reset drops the registrations inside it. */
    sl_texprov_note_pool(pool_a, sizeof pool_a);
    ck(sl_texprov_lookup(pool_a, 2, 1) == NULL, "after texInitPool over pool_a, 0x12's pointer is unregistered");
    ck(sl_texprov_lookup(pool_b, 2, 1) == NULL, "pool_b (COMMUNITY has no 0x34) still NULL");
    sl_texprov_note_load(pool_a, 64, 0x12, 2, 1);
    ck(sl_texprov_lookup(pool_a, 2, 1) != NULL, "re-registered: replaced again (the resident image, no re-read)");
    /* A re-registration of the same address for ANOTHER texture wins. */
    sl_texprov_note_load(pool_a, 64, 0x34, 2, 1);
    ck(sl_texprov_lookup(pool_a, 2, 1) == NULL, "pool_a now holds 0x34: COMMUNITY has none -> NULL");

    /* 8. The budget: 8 MB; 1024x1024 images are 4 MB each, so the third
     *    evicts the least recently used, and a re-ask re-reads the file. */
    {
        static unsigned char bigpx[1024 * 1024 * 4];
        unsigned i;
        unsigned char *bp[3] = { pool_a + 16, pool_b + 16, pool_c + 16 };
        for (i = 0; i < 3; i++) {
            mk_hdr(hdr, "SLTX", 1, 0x100u + i, 8, 8, 1024, 1024, 0, 1024u * 1024u * 4u);
            sprintf(path, "%s\\%04x.sltx", dir_c, 0x100u + i);
            ck(write_file(path, hdr, bigpx, 1024u * 1024u * 4u, 0), "write a 4 MB fixture");
            sl_texprov_note_load(bp[i], 64, (int) (0x100u + i), 8, 8);
        }
        ck(sl_texprov_lookup(bp[0], 8, 8) != NULL, "4 MB image 1 resident");
        ck(sl_texprov_lookup(bp[1], 8, 8) != NULL, "4 MB image 2 resident (8 MB used)");
        ck(sl_texprov_lookup(bp[2], 8, 8) != NULL, "4 MB image 3 resident");
        ck(img_find(img_key(SL_TEXTURES_COMMUNITY, 0x100u), 0)->state != IMG_LOADED
           && img_find(img_key(SL_TEXTURES_COMMUNITY, 0x102u), 0)->state == IMG_LOADED
           && g_res_bytes <= budget_bytes() && g_evicted >= 1u,
           "image 1 (least recently used) evicted, image 3 resident, the budget held");
        ck(sl_texprov_lookup(bp[0], 8, 8) != NULL, "image 1 asked again: re-read from the file");
        ck(g_reloaded == 1u && img_find(img_key(SL_TEXTURES_COMMUNITY, 0x101u), 0)->state != IMG_LOADED,
           "the re-ask counted a reload and evicted image 2 (now the least recent)");
        for (i = 0; i < 3; i++) { sprintf(path, "%s\\%04x.sltx", dir_c, 0x100u + i); remove(path); }
    }

    /* 9. The 8x4 asymmetric fixture, both alpha classes, survive a round
     *    trip through the loader intact (the cutout column stays 0). */
    mk_pixels(big, 8, 4, 1);
    mk_hdr(hdr, "SLTX", 1, 0x200, 4, 2, 8, 4, 0, 128);
    sprintf(path, "%s\\0200.sltx", dir_c); ck(write_file(path, hdr, big, 128, 0), "write 0200 (8x4 cutout)");
    sl_texprov_note_load(pool_c + 32, 8, 0x200, 4, 2);
    img = sl_texprov_lookup(pool_c + 32, 4, 2);
    ck(img != NULL && memcmp(img->rgba, big, 128) == 0 && img->rgba[3] == 0 && img->rgba[7] == 255,
       "8x4 cutout: bytes identical, column 0 alpha 0, column 1 alpha 255");

    sl_texprov_report(stdout);
    sprintf(path, "%s\\0012.sltx", dir_c); remove(path);
    sprintf(path, "%s\\0056.sltx", dir_c); remove(path);
    sprintf(path, "%s\\0200.sltx", dir_c); remove(path);
    sprintf(path, "%s\\0034.sltx", dir_x); remove(path);
    sprintf(path, "%s\\0012.sltx", dir_x); remove(path);
    _rmdir(dir_c); _rmdir(dir_x); _rmdir(root);
    printf("sl_texprov selftest: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
#endif /* SL_TEXPROV_SELFTEST */
