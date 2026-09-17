/**
 * Sightline — native asset overrides: path resolution, loading, validation.
 *
 * See src/sl_asset_override.h for what this seam is and why it is one seam
 * rather than two hacks. This file owns everything except the drawing, which
 * lives in src/gfx/sl_gfx_dl.c because that is where the display-list walker's
 * matrix stack and GL state cache are.
 *
 * NEVER TRUST THE FILE. Every field arrives from outside the program: magic,
 * version, declared file length against the real one, every count against an
 * explicit cap, every offset and extent against the file, every index against
 * the vertex count, every material index against the material count, every
 * texture index against the texture count, every texture's dimensions and its
 * decoded byte count. Multiplications are checked for overflow BEFORE they are
 * performed, not after. A file that fails any of these produces ONE warning
 * line and the original asset - never a crash and never an empty screen.
 *
 * NATIVE ONLY: the whole file compiles away on IDO, and src/native is outside
 * the cartridge Makefile's globs in any case.
 */
#ifndef __sgi

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ultra64.h>

#include "../sl_asset_override.h"

/* ------------------------------------------------------------------ ids -- */

/* The id -> relative path mapping, and the ONLY place it exists. The importer
 * derives the same path from the same id strings; see the note on the
 * resolution rule below. */
static const char *const sl_aov_relpath[SL_ASSET_ID_COUNT] = {
    "boot/nintendo_logo.slmodel",     /* boot.nintendo_logo  */
    "boot/rareware_logo.slmodel",     /* boot.rareware_logo  */
    "boot/goldeneye_logo.slmodel",    /* boot.goldeneye_logo */
    "boot/legal_page.slmodel"         /* boot.legal_page     */
};

static const char *const sl_aov_idname[SL_ASSET_ID_COUNT] = {
    "boot.nintendo_logo",
    "boot.rareware_logo",
    "boot.goldeneye_logo",
    "boot.legal_page"
};

/* ------------------------------------------------------- path resolution -- */

/*
 * ONE rule, shared with tools/asset/gltf_import.py. If these two ever
 * disagree the importer writes a file the runtime cannot find, which is a
 * silent no-op - the worst possible failure for an opt-in feature.
 *
 * THE INSTALL DIRECTORY - where the importer writes, and where a player's
 * own model goes - is unchanged:
 *
 *   1. $SL_ASSET_OVERRIDE_DIR, if set and non-empty
 *   2. %LOCALAPPDATA%\sightline\assets
 *   3. %USERPROFILE%\AppData\Local\sightline\assets
 *   4. %TEMP%\sightline\assets
 *
 * That ladder is not invented here: it is the one play.ps1 already uses for
 * the EEPROM (tools/windows/play.ps1:206), the one agent-parity.ps1 uses for
 * its scratch save, and the one tools/export/logo_models.py uses for exports.
 * A player's overrides are player data and belong outside the repository for
 * the same reason the save does.
 *
 * BELOW IT, AND ONLY BELOW IT, sits the COMMITTED directory: the models that
 * ship with the repository, in data/asset-overrides/. The order is the whole point:
 *
 *     install directory   ->   data/asset-overrides   ->   the original asset
 *
 * A player who installs their own model REPLACES a shipped one, because their
 * copy is found first. Nothing has to be uninstalled and nothing is
 * overwritten - the shipped file stays where it is, and deleting the player's
 * copy brings it back. The reverse order would let the repository silently
 * override the player, which is the wrong way round.
 *
 * FINDING data/asset-overrides WITHOUT A HARDCODED PATH. It is located from the
 * EXECUTABLE's own location - never from a compiled-in absolute path, which
 * would bake one machine's directory into every binary, and never from the
 * working directory, which is wherever the player happened to launch from:
 *
 *   <exe dir>\..\..\data\asset-overrides   a source tree (build\win32\sightline.exe)
 *   <exe dir>\data\asset-overrides         a packaged layout, data beside the exe
 *
 * Both are tried. A build that cannot determine its own path simply has no
 * committed step, which degrades to exactly the behaviour before this
 * existed.
 */
static char  g_aov_dir[512];
static int   g_aov_dir_done;
static char  g_aov_repo[2][512];
static int   g_aov_repo_n = -1;

static void aov_join(char *out, unsigned int cap, const char *a, const char *b)
{
    unsigned int n;

    out[0] = '\0';
    if (a == NULL) a = "";
    n = (unsigned int) strlen(a);
    if (n + 1u >= cap) return;
    memcpy(out, a, n);
    if (n != 0u && out[n - 1u] != '\\' && out[n - 1u] != '/') out[n++] = '\\';
    if (n + (unsigned int) strlen(b) + 1u > cap) { out[0] = '\0'; return; }
    strcpy(out + n, b);
}

const char *sl_asset_override_dir(void)
{
    const char *e;

    if (g_aov_dir_done) return g_aov_dir;
    g_aov_dir_done = 1;

    e = getenv("SL_ASSET_OVERRIDE_DIR");
    if (e != NULL && e[0] != '\0') {
        if (strlen(e) + 1u < sizeof g_aov_dir) { strcpy(g_aov_dir, e); return g_aov_dir; }
    }
    e = getenv("LOCALAPPDATA");
    if (e == NULL || e[0] == '\0') {
        static char up[400];
        const char *u = getenv("USERPROFILE");
        if (u != NULL && u[0] != '\0') {
            aov_join(up, sizeof up, u, "AppData\\Local");
            if (up[0] != '\0') e = up;
        }
    }
    if (e == NULL || e[0] == '\0') e = getenv("TEMP");
    if (e == NULL || e[0] == '\0') e = ".";
    aov_join(g_aov_dir, sizeof g_aov_dir, e, "sightline\\assets");
    if (g_aov_dir[0] == '\0') strcpy(g_aov_dir, ".");
    return g_aov_dir;
}

/*
 * The directory holding this executable, or 0 when it cannot be known.
 *
 * Win32 has one right answer (GetModuleFileNameA) and Linux has another
 * (/proc/self/exe). Neither is reached through a header here: <windows.h>
 * and the N64 SDK's <ultra64.h> disagree about several basic typedefs, and
 * this file needs exactly one function from it. The declaration below is the
 * documented signature, spelled without the header.
 */
#ifdef _WIN32
__declspec(dllimport) unsigned long __stdcall
GetModuleFileNameA(void *hModule, char *lpFilename, unsigned long nSize);
#endif

static int aov_exe_dir(char *out, unsigned int cap)
{
    unsigned int n = 0u;

    if (out == NULL || cap < 2u) return 0;
    out[0] = '\0';
#ifdef _WIN32
    n = (unsigned int) GetModuleFileNameA(NULL, out, (unsigned long) cap);
    /* GetModuleFileNameA returns the copied length and, when the buffer was
     * too small, that length is cap with the result TRUNCATED - which would
     * be a wrong path rather than no path. Treat it as no path. */
    if (n == 0u || n >= cap) { out[0] = '\0'; return 0; }
#else
    {
        FILE *f = fopen("/proc/self/exe", "rb");
        if (f != NULL) fclose(f);   /* readlink is not in the SDK headers */
        {
            extern long readlink(const char *, char *, unsigned long);
            long r = readlink("/proc/self/exe", out, (unsigned long) cap - 1ul);
            if (r <= 0 || (unsigned long) r >= (unsigned long) cap - 1ul) {
                out[0] = '\0'; return 0;
            }
            out[r] = '\0';
            n = (unsigned int) r;
        }
    }
#endif
    /* Strip the file name, leaving the directory. */
    while (n > 0u && out[n - 1u] != '\\' && out[n - 1u] != '/') n--;
    while (n > 1u && (out[n - 1u] == '\\' || out[n - 1u] == '/')) n--;
    out[n] = '\0';
    return out[0] != '\0';
}

/* The committed-override directories, in priority order. Computed once. */
static int aov_repo_dirs(void)
{
    char exe[420];
    char up[460];

    if (g_aov_repo_n >= 0) return g_aov_repo_n;
    g_aov_repo_n = 0;
    if (!aov_exe_dir(exe, sizeof exe)) return 0;

    /* build\\win32\\sightline.exe  ->  the repository root two levels up. */
    aov_join(up, sizeof up, exe, "..");
    if (up[0] != '\0') {
        char up2[500];
        aov_join(up2, sizeof up2, up, "..");
        if (up2[0] != '\0') {
            aov_join(g_aov_repo[g_aov_repo_n], sizeof g_aov_repo[0], up2,
                     "data\\asset-overrides");
            if (g_aov_repo[g_aov_repo_n][0] != '\0') g_aov_repo_n++;
        }
    }
    /* A packaged layout: data\\asset-overrides sitting beside the executable. */
    aov_join(g_aov_repo[g_aov_repo_n], sizeof g_aov_repo[0], exe,
             "data\\asset-overrides");
    if (g_aov_repo[g_aov_repo_n][0] != '\0') g_aov_repo_n++;
    return g_aov_repo_n;
}

static int aov_readable(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return 0;
    fclose(f);
    return 1;
}

/*
 * Candidate n on the ladder, counting from 0. Candidate 0 is the install
 * directory; the rest are the committed directories.
 *
 * The caller TRIES THEM IN ORDER and stops at the first that LOADS - not at
 * the first that exists. The difference matters exactly once, and it is the
 * upgrade case: a player carrying a model installed by an older importer has
 * a file at candidate 0 that this build cannot read. Stopping there would
 * leave them with the ORIGINAL logo and a version warning, having shipped
 * them a perfectly good model one directory further down. So a rejected
 * candidate says so and the search continues. The warning is still printed,
 * because the stale file is still there and they should know.
 */
static int aov_candidate(int id, int n, char *out, unsigned int cap)
{
    if (id < 0 || id >= SL_ASSET_ID_COUNT || out == NULL || cap == 0u) return 0;
    out[0] = '\0';
    if (n == 0) {
        aov_join(out, cap, sl_asset_override_dir(), sl_aov_relpath[id]);
    } else {
        if (n - 1 >= aov_repo_dirs()) return 0;
        aov_join(out, cap, g_aov_repo[n - 1], sl_aov_relpath[id]);
    }
    return out[0] != '\0';
}

int sl_asset_override_path(int id, char *out, unsigned int cap)
{
    int n;

    if (id < 0 || id >= SL_ASSET_ID_COUNT || out == NULL || cap == 0u) return 0;

    for (n = 0; ; n++) {
        if (!aov_candidate(id, n, out, cap)) break;
        if (aov_readable(out)) return 1;
    }
    /* Nothing installed anywhere. Report the INSTALL path regardless, so a
     * diagnostic says where a file would go rather than nothing at all. */
    return aov_candidate(id, 0, out, cap);
}

/* ------------------------------------------------------------- decoding -- */

static unsigned int rd32(const unsigned char *p)
{
    return (unsigned int) p[0] | ((unsigned int) p[1] << 8)
         | ((unsigned int) p[2] << 16) | ((unsigned int) p[3] << 24);
}

/* IEEE-754 single, little-endian on disk. Assembled from bytes and memcpy'd
 * rather than cast through a float*, so the reader does not depend on the
 * host's byte order or on type punning through a pointer. */
static float rdf32(const unsigned char *p)
{
    unsigned int u = rd32(p);
    float f;
    memcpy(&f, &u, sizeof f);
    return f;
}

/* a*b with overflow detected before it happens. */
static int mul_ok(unsigned int a, unsigned int b, unsigned int *out)
{
    if (a != 0u && b > 0xFFFFFFFFu / a) return 0;
    *out = a * b;
    return 1;
}

/* Does [off, off+len) lie inside the file, without wrapping? */
static int span_ok(unsigned long off, unsigned int len, unsigned long flen)
{
    if (off > flen) return 0;
    if ((unsigned long) len > flen - off) return 0;
    return 1;
}

/* ------------------------------------------------------------- registry -- */

/*
 * SLOT STATES. The extra one, PENDING, exists because a texture REFERENCE
 * resolves out of game data that may not be resident when the file is first
 * read - and "not yet" is not the same failure as "never".
 *
 *    0  untried        the file has not been opened
 *    1  ready          loaded, and every reference resolved
 *   -1  unavailable    absent, malformed, or permanently unresolvable
 *    2  pending        loaded and valid, references still unresolved
 *
 * A PENDING slot reports itself unavailable - so the ORIGINAL asset draws,
 * which is the correct fallback - and retries on the next call. The retry is
 * bounded: after AOV_RESOLVE_TRIES attempts it becomes permanently
 * unavailable with ONE warning, so a player with no ROM gets a single line
 * and the original logos, not a message every frame forever.
 *
 * The file is read ONCE. Only the resolution is retried, so a retry costs
 * arithmetic, not a re-read of a multi-megabyte model.
 */
#define AOV_RESOLVE_TRIES 60

struct sl_aov_slot {
    int             state;      /* see above */
    int             tries;      /* resolution attempts made so far */
    char            path[600];  /* the file this slot came from */
    struct sl_amdl  m;
};

static struct sl_aov_slot g_aov[SL_ASSET_ID_COUNT];

static void aov_free(struct sl_amdl *m)
{
    if (m->tex != NULL) {
        unsigned int i;
        /* Resolved references own their decoded pixels; embedded slots point
         * into the blob and must not be freed separately. */
        for (i = 0u; i < m->ntex; i++)
            if (m->tex[i].owned != NULL) free(m->tex[i].owned);
    }
    if (m->blob != NULL) free(m->blob);
    if (m->prim != NULL) free(m->prim);
    if (m->mat  != NULL) free(m->mat);
    if (m->tex  != NULL) free(m->tex);
    memset(m, 0, sizeof *m);
}

/* ONE line per asset load event, ever - success, absence or rejection. No
 * per-frame logging exists anywhere in this seam; normal play stays quiet, and
 * a boot with no overrides installed prints nothing at all. */
static void aov_reject(int id, struct sl_amdl *m, const char *path,
                       const char *why)
{
    aov_free(m);
    fprintf(stderr, "sl_asset: %s override at %s is unusable (%s)"
                    " - drawing the original\n",
            sl_aov_idname[id], path, why);
}

static int aov_load_from(int id, const char *path)
{
    FILE          *f;
    unsigned char *blob = NULL;
    unsigned long  flen;
    long           tell;
    struct sl_amdl m;
    unsigned int   need, i, k;
    unsigned int   off_pos, off_nrm, off_uv, off_col, off_idx;
    unsigned int   off_prim, off_mat, off_tex, declared;
    unsigned int   texbytes = 0u;
    unsigned int   nref = 0u;

    memset(&m, 0, sizeof m);

    f = fopen(path, "rb");
    if (f == NULL) return 0;            /* the ordinary case: no override */

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
    tell = ftell(f);
    if (tell < 0) { fclose(f); return 0; }
    flen = (unsigned long) tell;
    if (flen < SL_AMDL_HEADER_SIZE || flen > SL_AMDL_MAX_FILE) {
        fclose(f);
        fprintf(stderr, "sl_asset: %s override at %s is unusable"
                        " (file length %lu outside 128..%u) - drawing the"
                        " original\n",
                sl_aov_idname[id], path, flen, (unsigned) SL_AMDL_MAX_FILE);
        return 0;
    }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return 0; }

    blob = (unsigned char *) malloc((size_t) flen);
    if (blob == NULL) { fclose(f); return 0; }
    if (fread(blob, 1, (size_t) flen, f) != (size_t) flen) {
        fclose(f); free(blob);
        fprintf(stderr, "sl_asset: %s override at %s is unusable"
                        " (short read) - drawing the original\n",
                sl_aov_idname[id], path);
        return 0;
    }
    fclose(f);
    m.blob = blob;
    m.blob_len = flen;

    if (blob[0] != SL_AMDL_MAGIC0 || blob[1] != SL_AMDL_MAGIC1
        || blob[2] != SL_AMDL_MAGIC2 || blob[3] != SL_AMDL_MAGIC3) {
        aov_reject(id, &m, path, "not an SLM1 model file");
        return 0;
    }
    /* The version is read out BEFORE anything is freed.
     *
     * MEASURED CRASH, and the reason this line looks the way it does: the
     * previous form called aov_free(&m) - which frees blob - and then read
     * rd32(blob + 4) again to fill in the message. A use-after-free, and one
     * that no run could reach until a build existed whose SL_AMDL_VERSION
     * differed from a file already installed. The first launch of the
     * version-2 build against the version-1 models on this machine faulted
     * here at 0xC0000005, with the frame naming rd32 called from
     * aov_load_from. It is fixed rather than worked around: the value is
     * taken while the buffer is unambiguously alive.
     *
     * The whole class is worth stating, because this file frees inside its
     * rejection helpers: NOTHING MAY READ THE BLOB AFTER aov_free. */
    {
        unsigned int filever = rd32(blob + 4);
        if (filever != SL_AMDL_VERSION) {
            aov_free(&m);
            fprintf(stderr, "sl_asset: %s override at %s is version %u; this"
                            " build reads version %u - drawing the original."
                            " Re-run tools\\windows\\asset-import.ps1.\n",
                    sl_aov_idname[id], path, filever,
                    (unsigned) SL_AMDL_VERSION);
            return 0;
        }
    }
    if (rd32(blob + 8) != SL_AMDL_HEADER_SIZE) {
        aov_reject(id, &m, path, "header size field is wrong");
        return 0;
    }
    declared = rd32(blob + 68);
    if ((unsigned long) declared != flen) {
        aov_reject(id, &m, path, "declared file size does not match the file");
        return 0;
    }

    m.flags = rd32(blob + 12);
    m.nvert = rd32(blob + 16);
    m.nidx  = rd32(blob + 20);
    m.nprim = rd32(blob + 24);
    m.nmat  = rd32(blob + 28);
    m.ntex  = rd32(blob + 32);
    off_pos  = rd32(blob + 36);
    off_nrm  = rd32(blob + 40);
    off_uv   = rd32(blob + 44);
    off_col  = rd32(blob + 48);
    off_idx  = rd32(blob + 52);
    off_prim = rd32(blob + 56);
    off_mat  = rd32(blob + 60);
    off_tex  = rd32(blob + 64);

    if (m.nvert == 0u || m.nvert > SL_AMDL_MAX_VERTS) {
        aov_reject(id, &m, path, "vertex count out of range"); return 0; }
    if (m.nidx == 0u || m.nidx > SL_AMDL_MAX_INDICES || (m.nidx % 3u) != 0u) {
        aov_reject(id, &m, path, "index count out of range or not a multiple of 3");
        return 0; }
    if (m.nprim == 0u || m.nprim > SL_AMDL_MAX_PRIMS) {
        aov_reject(id, &m, path, "primitive count out of range"); return 0; }
    if (m.nmat == 0u || m.nmat > SL_AMDL_MAX_MATS) {
        aov_reject(id, &m, path, "material count out of range"); return 0; }
    if (m.ntex > SL_AMDL_MAX_TEXS) {
        aov_reject(id, &m, path, "texture count out of range"); return 0; }

    /* --- attribute arrays ------------------------------------------------ */
    if (!mul_ok(m.nvert, 12u, &need) || !span_ok(off_pos, need, flen)) {
        aov_reject(id, &m, path, "position array does not fit the file"); return 0; }
    m.pos = (const float *) (blob + off_pos);

    if (m.flags & SL_AMDL_F_NORMALS) {
        if (!mul_ok(m.nvert, 12u, &need) || !span_ok(off_nrm, need, flen)) {
            aov_reject(id, &m, path, "normal array does not fit the file"); return 0; }
        m.nrm = (const float *) (blob + off_nrm);
    }
    if (m.flags & SL_AMDL_F_UV) {
        if (!mul_ok(m.nvert, 8u, &need) || !span_ok(off_uv, need, flen)) {
            aov_reject(id, &m, path, "uv array does not fit the file"); return 0; }
        m.uv = (const float *) (blob + off_uv);
    }
    if (m.flags & SL_AMDL_F_COLOR) {
        if (!mul_ok(m.nvert, 4u, &need) || !span_ok(off_col, need, flen)) {
            aov_reject(id, &m, path, "colour array does not fit the file"); return 0; }
        m.col = blob + off_col;
    }
    if (!mul_ok(m.nidx, 4u, &need) || !span_ok(off_idx, need, flen)) {
        aov_reject(id, &m, path, "index array does not fit the file"); return 0; }
    m.idx = (const unsigned int *) (blob + off_idx);

    if (!mul_ok(m.nprim, 16u, &need) || !span_ok(off_prim, need, flen)) {
        aov_reject(id, &m, path, "primitive table does not fit the file"); return 0; }
    if (!mul_ok(m.nmat, 32u, &need) || !span_ok(off_mat, need, flen)) {
        aov_reject(id, &m, path, "material table does not fit the file"); return 0; }
    if (m.ntex != 0u) {
        /* 32 bytes per slot in version 2 - see the layout in
         * src/sl_asset_override.h. The importer writes the same stride and
         * the test suite reads it back with the same number. */
        if (!mul_ok(m.ntex, 32u, &need) || !span_ok(off_tex, need, flen)) {
            aov_reject(id, &m, path, "texture table does not fit the file"); return 0; }
    }

    /* --- indices --------------------------------------------------------
     *
     * Bounds-checked here, once, so the renderer's inner loop can trust them.
     * The alternative - checking per draw - is the same test run sixty times a
     * second for a file that cannot change between frames. */
    for (i = 0u; i < m.nidx; i++) {
        if (rd32(blob + off_idx + i * 4u) >= m.nvert) {
            aov_reject(id, &m, path, "a triangle index is outside the vertex array");
            return 0;
        }
    }

    /* --- textures --------------------------------------------------------
     *
     * A slot is EITHER embedded pixels OR a reference to a game texture, and
     * the two are validated differently: embedded pixels must fit the file,
     * a reference must name something. Both must declare dimensions inside
     * the limits, and both count against the same decoded-bytes budget - a
     * reference costs exactly as much memory once resolved, so exempting it
     * from the budget would make the cap a lie. */
    if (m.ntex != 0u) {
        m.tex = (struct sl_amdl_tex *) calloc(m.ntex, sizeof *m.tex);
        if (m.tex == NULL) { aov_free(&m); return 0; }
        for (i = 0u; i < m.ntex; i++) {
            const unsigned char *r = blob + off_tex + i * 32u;
            unsigned int w = rd32(r), h = rd32(r + 4);
            unsigned int off = rd32(r + 8), len = rd32(r + 12);
            unsigned int kind = rd32(r + 16);
            unsigned int noff = rd32(r + 20), nlen = rd32(r + 24);
            unsigned int hash = rd32(r + 28);
            unsigned int px, bytes;

            if (w == 0u || h == 0u || w > SL_AMDL_MAX_TEXDIM
                || h > SL_AMDL_MAX_TEXDIM) {
                aov_reject(id, &m, path, "a texture dimension is out of range");
                return 0;
            }
            if (!mul_ok(w, h, &px) || !mul_ok(px, 4u, &bytes)) {
                aov_reject(id, &m, path, "a texture's size overflows");
                return 0;
            }
            if (bytes > SL_AMDL_MAX_TEXBYTES - texbytes) {
                aov_reject(id, &m, path, "decoded textures exceed the size limit");
                return 0;
            }
            texbytes += bytes;

            if (kind == SL_AMDL_TEX_EMBEDDED) {
                if (bytes != len) {
                    aov_reject(id, &m, path, "a texture's byte length does not"
                                             " match its dimensions");
                    return 0;
                }
                if (!span_ok(off, len, flen)) {
                    aov_reject(id, &m, path, "a texture's pixels do not fit the"
                                             " file");
                    return 0;
                }
                if (noff != 0u || nlen != 0u) {
                    aov_reject(id, &m, path, "an embedded texture also names an"
                                             " identifier");
                    return 0;
                }
                m.tex[i].rgba = blob + off;
            } else if (kind == SL_AMDL_TEX_REF) {
                const char *nm;
                unsigned int k;

                /* A reference carries no pixels. Saying otherwise means the
                 * file is not what it claims to be. */
                if (off != 0u || len != 0u) {
                    aov_reject(id, &m, path, "a texture reference also carries"
                                             " pixel data");
                    return 0;
                }
                if (nlen == 0u || nlen > SL_AMDL_MAX_TEXNAME) {
                    aov_reject(id, &m, path, "a texture identifier's length is"
                                             " out of range");
                    return 0;
                }
                /* nlen + 1 for the NUL, which must be inside the file too. */
                if (!span_ok(noff, nlen + 1u, flen)) {
                    aov_reject(id, &m, path, "a texture identifier does not fit"
                                             " the file");
                    return 0;
                }
                nm = (const char *) (blob + noff);
                if (nm[nlen] != '\0') {
                    aov_reject(id, &m, path, "a texture identifier is not"
                                             " NUL-terminated");
                    return 0;
                }
                /* The identifier is compared with strcmp and printed in a
                 * warning, so its bytes are constrained rather than trusted:
                 * an embedded NUL would truncate the comparison, and a
                 * control byte would corrupt the diagnostic it appears in. */
                for (k = 0u; k < nlen; k++) {
                    char c = nm[k];
                    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
                          || c == '.' || c == '_')) {
                        aov_reject(id, &m, path, "a texture identifier contains"
                                                 " characters that are not"
                                                 " allowed");
                        return 0;
                    }
                }
                m.tex[i].name = nm;
                m.tex[i].hash = hash;
                m.tex[i].rgba = NULL;   /* filled in by aov_resolve */
                nref++;
            } else {
                aov_reject(id, &m, path, "a texture slot has an unknown kind");
                return 0;
            }
            m.tex[i].w = w;
            m.tex[i].h = h;
            m.tex[i].kind = kind;
            m.tex[i].gl = 0u;
        }
    }

    /* --- materials ------------------------------------------------------- */
    m.mat = (struct sl_amdl_mat *) calloc(m.nmat, sizeof *m.mat);
    if (m.mat == NULL) { aov_free(&m); return 0; }
    for (i = 0u; i < m.nmat; i++) {
        const unsigned char *r = blob + off_mat + i * 32u;
        int t;
        for (k = 0u; k < 4u; k++) m.mat[i].base[k] = rdf32(r + k * 4u);
        t = (int) rd32(r + 16);
        if (t < -1 || (t >= 0 && (unsigned int) t >= m.ntex)) {
            aov_reject(id, &m, path, "a material names a texture that does not exist");
            return 0;
        }
        m.mat[i].texture = t;
        m.mat[i].flags = rd32(r + 20);
        m.mat[i].alpha_cutoff = rdf32(r + 24);

        /* ---- generated coordinates: resolve the policy ONCE, here --------
         *
         * The draw path must not re-derive this. It is decided at load, from
         * two inputs in a fixed order, and stored as one int the renderer
         * reads - so "why is this material sweeping" has exactly one answer
         * and one place to look for it.
         *
         *   1. An EXPLICIT flag from the author wins outright.
         *   2. Otherwise it is IMPLIED by the texture slot: a REFERENCE to a
         *      game texture the registry marks as generated-coordinate.
         *
         * Both flags at once is REJECTED rather than resolved. A file that
         * says on and off about the same material is not expressing a
         * preference this loader is entitled to guess at, and picking either
         * one would be a silent wrong answer of exactly the kind every other
         * check in this file exists to prevent. */
        if ((m.mat[i].flags & SL_AMDL_M_TEXGEN)
            && (m.mat[i].flags & SL_AMDL_M_TEXGEN_OFF)) {
            aov_reject(id, &m, path, "a material both forces generated texture"
                                     " coordinates on and forces them off");
            return 0;
        }
        if (m.mat[i].flags & SL_AMDL_M_TEXGEN_OFF) {
            m.mat[i].texgen = 0;
        } else if (m.mat[i].flags & SL_AMDL_M_TEXGEN) {
            m.mat[i].texgen = 1;
        } else {
            m.mat[i].texgen = (t >= 0
                               && m.tex[t].kind == SL_AMDL_TEX_REF
                               && sl_texref_is_generated(m.tex[t].name));
        }
    }

    /* ONE line, and only when something is generated. The census style the
     * rest of this file uses: a count of zero and a count never taken look
     * identical in a log, so a model that generates nothing says nothing here
     * and a model that does says how many materials and on what grounds. */
    {
        unsigned gen = 0u, implied = 0u, forced = 0u;
        for (i = 0u; i < m.nmat; i++) {
            if (!m.mat[i].texgen) continue;
            gen++;
            if (m.mat[i].flags & SL_AMDL_M_TEXGEN) forced++; else implied++;
        }
        if (gen != 0u)
            fprintf(stderr, "sl_asset: %s takes per-frame generated texture"
                            " coordinates on %u of %u material(s)"
                            " (%u implied by a game-texture reference,"
                            " %u declared)\n",
                    sl_aov_idname[id], gen, m.nmat, implied, forced);
    }

    /* --- primitives ------------------------------------------------------ */
    m.prim = (struct sl_amdl_prim *) calloc(m.nprim, sizeof *m.prim);
    if (m.prim == NULL) { aov_free(&m); return 0; }
    for (i = 0u; i < m.nprim; i++) {
        const unsigned char *r = blob + off_prim + i * 16u;
        unsigned int first = rd32(r), count = rd32(r + 4), mat = rd32(r + 8);

        if (count == 0u || (count % 3u) != 0u || first > m.nidx
            || count > m.nidx - first) {
            aov_reject(id, &m, path, "a primitive's index range is outside the"
                                     " index array");
            return 0;
        }
        if (mat >= m.nmat) {
            aov_reject(id, &m, path, "a primitive names a material that does not"
                                     " exist");
            return 0;
        }
        m.prim[i].first = first;
        m.prim[i].count = count;
        m.prim[i].material = mat;
    }

    g_aov[id].m = m;
    if (strlen(path) + 1u <= sizeof g_aov[id].path)
        strcpy(g_aov[id].path, path);
    fprintf(stderr, "sl_asset: %s override loaded from %s"
                    " - %u verts, %u tris, %u prims, %u mats, %u tex"
                    " (%u embedded, %u referenced)\n",
            sl_aov_idname[id], path, m.nvert, m.nidx / 3u, m.nprim, m.nmat,
            m.ntex, m.ntex - nref, nref);
    return 1;
}

/* ------------------------------------------------------ reference resolve --
 *
 * Every unresolved reference in the model, turned into pixels, or nothing.
 * ALL-OR-NOTHING is deliberate: a model with one slot resolved and one not
 * would draw part-textured, which looks like a rendering bug rather than a
 * missing ROM. Either the whole custom model is correct or the ORIGINAL
 * asset draws.
 *
 * THE HASH CHECK is what makes a reference safe rather than merely
 * convenient. The importer recorded FNV-1a over the very pixels it refused
 * to embed; this recomputes it over what was resolved. Equal means the
 * player's game data holds exactly the texture the author was looking at.
 * Unequal means it does not - a different region, a different revision, a
 * ROM hack - and that is a clean fallback to the original logo instead of a
 * model wearing the wrong picture.
 *
 * Returns 1 when the model is fully resolved, 0 when it is not yet, and -1
 * when it never will be.
 */
static int aov_resolve(int id)
{
    struct sl_amdl *m = &g_aov[id].m;
    unsigned int i;

    for (i = 0u; i < m->ntex; i++) {
        struct sl_amdl_tex *t = &m->tex[i];
        unsigned char *rgba = NULL;
        unsigned int w = 0u, h = 0u, got;

        if (t->kind != SL_AMDL_TEX_REF || t->owned != NULL) continue;

        if (!sl_texref_resolve(t->name, &w, &h, &rgba))
            return 0;                    /* not yet - the caller retries */

        /* The importer wrote the dimensions it saw. A resolved texture of a
         * different size is not the one the model was built against, and its
         * UVs would not fit it. */
        if (w != t->w || h != t->h) {
            free(rgba);
            fprintf(stderr, "sl_asset: %s names game texture '%s' as %ux%u"
                            " but this build resolves it as %ux%u"
                            " - drawing the original\n",
                    sl_aov_idname[id], t->name, t->w, t->h, w, h);
            return -1;
        }
        got = sl_amdl_fnv1a(rgba, (unsigned long) w * (unsigned long) h * 4ul);
        if (got != t->hash) {
            free(rgba);
            fprintf(stderr, "sl_asset: %s names game texture '%s', but this"
                            " copy of the game has different pixels there"
                            " (expected %08x, found %08x) - drawing the"
                            " original\n",
                    sl_aov_idname[id], t->name, t->hash, got);
            return -1;
        }
        /* ONE line per reference, once, when it resolves. This is a load
         * event like the model load itself, not per-frame chatter - and it is
         * the positive evidence that the seam worked: without it a resolved
         * reference and a silently-wrong one look identical in a log. */
        {
            /* The attempt number is the LOAD-ORDER measurement. A 1 says the
             * game data was already resident the first time this asset was
             * asked for - which is what the call sites predict, both hooks
             * being constructors that run after the screen's init - and a
             * larger number would say the retry loop is load-bearing rather
             * than defensive. The record index says WHEN, in the units the
             * screenshot window (SL_SHOT_READ_FIRST/LAST) uses, so a claim
             * about a screen can be checked against a picture of it. */
            extern unsigned sl_record_index(void);
            fprintf(stderr, "sl_asset: %s resolved game texture '%s'"
                            " - %ux%u, fnv %08x matches"
                            " (attempt %d, record index %u)\n",
                    sl_aov_idname[id], t->name, w, h, got,
                    g_aov[id].tries + 1, sl_record_index());
        }
        t->owned = rgba;
        t->rgba  = rgba;
    }
    return 1;
}

/* Every candidate on the ladder, in order, until one loads. */
static int aov_load(int id)
{
    char path[600];
    int  n;

    for (n = 0; ; n++) {
        if (!aov_candidate(id, n, path, sizeof path)) return 0;
        if (aov_load_from(id, path)) return 1;
    }
}

/*
 * THE OFF SWITCH.  SL_ASSET_OVERRIDES=0 turns the whole seam off.
 *
 * This exists because the feature's default changed. While overrides lived
 * only in the player's install directory, 'off' was the absence of a file
 * and -Remove was the off switch. Now that models SHIP IN THE REPOSITORY, a
 * fresh clone has them on by default and there is nothing to delete - the
 * committed file is repository content, not player data, and asking someone
 * to delete a tracked file to see the original logos would be a dirty tree
 * every time.
 *
 * The check is FIRST, before path resolution, before any stat, before any
 * read. With it set, this function is a load of a cached int and a compare,
 * and the two call sites take the same else-arm they take on a machine that
 * has never heard of overrides: the original asset, the original display
 * list, the original timing. That is the opt-out contract the seam was built
 * with, restated for a default that changed.
 */
static int aov_enabled(void)
{
    static int on = -1;
    if (on < 0) {
        const char *v = getenv("SL_ASSET_OVERRIDES");
        on = !(v != NULL && v[0] == '0' && v[1] == '\0');
    }
    return on;
}

int sl_asset_override_available(int id)
{
    struct sl_aov_slot *sl;

    if (id < 0 || id >= SL_ASSET_ID_COUNT) return 0;
    if (!aov_enabled()) return 0;
    sl = &g_aov[id];

    if (sl->state == 0)
        sl->state = aov_load(id) ? 2 : -1;   /* 2: references still pending */

    if (sl->state == 2) {
        int r = aov_resolve(id);
        if (r > 0) {
            sl->state = 1;
        } else if (r < 0) {
            aov_free(&sl->m);
            sl->state = -1;                  /* the message is already out */
        } else if (++sl->tries >= AOV_RESOLVE_TRIES) {
            fprintf(stderr, "sl_asset: %s at %s references game texture data"
                            " that never became available (%s)"
                            " - drawing the original\n",
                    sl_aov_idname[id], sl->path, sl_texref_why());
            aov_free(&sl->m);
            sl->state = -1;
        }
    }
    return sl->state == 1;
}

const struct sl_amdl *sl_asset_override_get_model(int id)
{
    if (!sl_asset_override_available(id)) return NULL;
    return &g_aov[id].m;
}

void *sl_asset_override_emit(void *gdlv, int id, int fade)
{
    Gfx *gdl = (Gfx *) gdlv;

    if (gdl == NULL || id < 0 || id >= SL_ASSET_ID_COUNT) return gdlv;
    if (fade < 0) fade = 0;
    if (fade > 255) fade = 255;

    gdl->words.w0 = (uintptr_t) (SL_AOV_DL_W0_BASE | (unsigned int) id);
    gdl->words.w1 = (uintptr_t) (SL_AOV_DL_W1_BASE | (unsigned int) fade);
    gdl++;
    return (void *) gdl;
}

#endif /* !__sgi */
