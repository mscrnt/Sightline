/*
 * Sightline demo bootstrap - the self-extracting front half of SightlineDemo.exe.
 *
 * WHAT THIS IS
 *     One ordinary Win32 console program with the whole demo stapled to it as
 *     RCDATA resources: the demo core, its two non-system DLLs, the ROM, and
 *     the packaged boot assets. It unpacks them into a private, hash-addressed
 *     cache under %LOCALAPPDATA%, establishes exactly the environment the core
 *     needs, launches the core with CreateProcessW, waits, and returns the
 *     core's own exit code.
 *
 *     The user drops the one file in an empty folder and double-clicks it.
 *     No repository, no PowerShell, no MSYS2, no Python, no environment
 *     variables, no ROM selection, no DLL copying, no asset install step.
 *
 * WHAT THIS IS NOT
 *     Not an installer, not an updater, not a downloader, not a launcher menu.
 *     It writes nothing outside its own cache directory, creates no registry
 *     entries and no shortcuts, and has nothing to uninstall beyond deleting
 *     that directory.
 *
 * THIS FILE IS GENERIC AND CARRIES NO PAYLOAD
 *     Every payload-specific fact - which files, where they go, what they
 *     hash to, which one to launch, which environment variables to establish
 *     - arrives at runtime from the SL_MANIFEST resource that
 *     tools/windows/package-demo.ps1 generates. There is deliberately no file
 *     name, no absolute path, no hash and no byte of payload compiled in
 *     here, so this source is committable while the generated resource script
 *     and the finished executable are not (project rule 2). The repository
 *     contains no ROM: the one the packager embeds is supplied by the user,
 *     and the finished executable is private, local and git-ignored.
 *
 * THE MANIFEST FORMAT
 *     UTF-8 text, LF-separated lines, fields separated by single spaces.
 *     Blank lines and lines beginning with '#' are ignored.
 *
 *         id    <hex>                          the payload id; names the cache
 *         file  <RESNAME> <relpath> <size> <sha256hex>
 *         cwd   <relpath>                      working directory for the core
 *         launch <relpath>                     the core to run
 *         env   <NAME> <VALUE>                 set for the child; {ROOT}
 *                                              expands to the cache root
 *
 *     No field may contain a space, which the packager asserts before it
 *     writes the manifest. That keeps the parser here small enough to read in
 *     one sitting, which matters more than generality for a format with
 *     exactly one producer.
 *
 * WHY A HASH-ADDRESSED CACHE
 *     The cache directory is named after a SHA-256 aggregate of everything
 *     packaged, so a rebuilt demo with any changed byte extracts to a new
 *     directory and can never be confused with an older one, and a re-run of
 *     the same demo re-uses what is already on disk. Every file is verified
 *     against its recorded size and SHA-256 on every launch: a truncated
 *     write, a half-finished copy or a file someone edited is detected and
 *     replaced rather than run.
 *
 * WHY BCrypt
 *     SHA-256 has to come from somewhere, and this project does not add
 *     dependencies to solve problems the platform already solves (project
 *     rules, Dependencies). BCrypt/CNG ships with Windows; OpenSSL would be a new
 *     library to link, ship and keep current for one hash function.
 *
 * THE CONSOLE IS LOAD-BEARING - READ BEFORE "FIXING" THE BLACK WINDOW
 *     The core is built -mconsole on purpose (docs/decisions/windows-sdl-main.md)
 *     and it BRANCHES ON isatty: sl_run_open() captures a run only when a
 *     stream is a terminal, and reopens stderr into the run directory only
 *     when stderr is a terminal (src/platform/sl_main.c:213, :294). The
 *     accepted ordinary launch is `& $exe` from a PowerShell prompt, which
 *     gives the child that prompt's console. This bootstrap reproduces that:
 *     it is itself -mconsole and calls CreateProcessW with NO creation flags,
 *     so the core inherits this process's console and takes exactly the same
 *     branches.
 *
 *     Double-clicked from Explorer, Windows hands this process a console of
 *     its own, and an empty black window beside the game is not what a
 *     showcase should look like. So the window is HIDDEN - not closed, not
 *     detached, not replaced by -mwindows. The console stays attached, the
 *     standard handles stay valid, isatty stays true, and every branch above
 *     resolves the way it does for the owner. That is the only console change
 *     that costs nothing.
 *
 *     And it is done ONLY when this process is the console's sole client
 *     (GetConsoleProcessList returns 1), which is the Explorer case. Launched
 *     from an existing terminal the count is greater than one, the console
 *     belongs to the shell, and hiding it would hide the user's own window.
 *
 *     DO NOT link this -mwindows. That gives the core no console at all,
 *     isatty(2) becomes false, and the run capture silently changes
 *     behaviour - a different program from the one the owner accepted.
 */

/* GetConsoleProcessList and BCrypt both need a modern target; mingw's default
 * is not guaranteed to be one, and a silently missing prototype here becomes
 * an implicit int and a wrong answer rather than a build error. */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include <windows.h>
#include <bcrypt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#ifndef MOVEFILE_REPLACE_EXISTING
#define MOVEFILE_REPLACE_EXISTING 0x00000001
#endif
#ifndef MOVEFILE_WRITE_THROUGH
#define MOVEFILE_WRITE_THROUGH 0x00000008
#endif

/* NTSTATUS success is "not negative". Spelled here rather than relying on a
 * NT_SUCCESS macro that mingw's headers may or may not have exported. */
#define SL_NT_OK(s) (((NTSTATUS) (s)) >= 0)

#define SL_MAXPATH   1024
#define SL_MAXFILES  64
#define SL_MAXENV    32

typedef struct {
    char          res[128];        /* RCDATA resource name                  */
    wchar_t       rel[SL_MAXPATH]; /* path under the cache root             */
    unsigned long long size;
    unsigned char sha[32];
} sl_file;

typedef struct {
    wchar_t name[128];
    wchar_t value[SL_MAXPATH * 2];
} sl_env;

static sl_file g_file[SL_MAXFILES];
static int     g_nfile;
static sl_env  g_env[SL_MAXENV];
static int     g_nenv;
static char    g_id[128];
static wchar_t g_launch[SL_MAXPATH];
static wchar_t g_cwd[SL_MAXPATH];

static int     g_own_console;   /* this process is the console's only client */

/* ------------------------------------------------------------- reporting -- */

static void sl_say(const wchar_t *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfwprintf(stderr, fmt, ap);
    va_end(ap);
    fflush(stderr);
}

/*
 * A fatal error has to reach a person who double-clicked an icon. stderr is
 * the right channel when there is a terminal to read it; when this process
 * owns its console the window is about to be (or already is) hidden and
 * nobody would ever see the line, so the same text goes into a message box.
 * Not a dialog framework - one MessageBoxW on the failure path only.
 */
static int sl_fail(const wchar_t *fmt, ...)
{
    wchar_t buf[2048];
    va_list ap;

    va_start(ap, fmt);
    _vsnwprintf(buf, sizeof buf / sizeof buf[0] - 1, fmt, ap);
    va_end(ap);
    buf[sizeof buf / sizeof buf[0] - 1] = L'\0';

    fwprintf(stderr, L"SightlineDemo: %ls\n", buf);
    fflush(stderr);
    if (g_own_console)
        MessageBoxW(NULL, buf, L"Sightline Demo", MB_ICONERROR | MB_OK);
    return 1;
}

/* ---------------------------------------------------------------- sha256 -- */

/*
 * One algorithm handle for the whole run. Opening the provider per file is
 * measurable on a 12 MB ROM and buys nothing.
 */
static BCRYPT_ALG_HANDLE g_alg;

static int sl_sha_open(void)
{
    NTSTATUS s = BCryptOpenAlgorithmProvider(&g_alg, BCRYPT_SHA256_ALGORITHM,
                                             NULL, 0);
    return SL_NT_OK(s);
}

typedef struct {
    BCRYPT_HASH_HANDLE h;
    unsigned char     *obj;
} sl_hash;

static int sl_hash_begin(sl_hash *hs)
{
    DWORD objlen = 0, got = 0;
    NTSTATUS s;

    hs->h = NULL;
    hs->obj = NULL;
    s = BCryptGetProperty(g_alg, BCRYPT_OBJECT_LENGTH, (PUCHAR) &objlen,
                          sizeof objlen, &got, 0);
    if (!SL_NT_OK(s)) return 0;
    hs->obj = (unsigned char *) malloc(objlen);
    if (hs->obj == NULL) return 0;
    s = BCryptCreateHash(g_alg, &hs->h, hs->obj, objlen, NULL, 0, 0);
    if (!SL_NT_OK(s)) { free(hs->obj); hs->obj = NULL; return 0; }
    return 1;
}

static int sl_hash_feed(sl_hash *hs, const void *p, DWORD n)
{
    return SL_NT_OK(BCryptHashData(hs->h, (PUCHAR) p, n, 0));
}

static int sl_hash_end(sl_hash *hs, unsigned char out[32])
{
    NTSTATUS s = BCryptFinishHash(hs->h, out, 32, 0);
    BCryptDestroyHash(hs->h);
    hs->h = NULL;
    free(hs->obj);
    hs->obj = NULL;
    return SL_NT_OK(s);
}

static int sl_sha_mem(const void *p, size_t n, unsigned char out[32])
{
    sl_hash hs;
    const unsigned char *q = (const unsigned char *) p;

    if (!sl_hash_begin(&hs)) return 0;
    while (n > 0) {
        DWORD chunk = (n > 0x100000u) ? 0x100000u : (DWORD) n;
        if (!sl_hash_feed(&hs, q, chunk)) { sl_hash_end(&hs, out); return 0; }
        q += chunk;
        n -= chunk;
    }
    return sl_hash_end(&hs, out);
}

/*
 * Hash a file, and report its size while we are reading it. Returns 0 when
 * the file cannot be opened or read - which is a normal, expected answer
 * here, not an error: it is how "not extracted yet" is detected.
 */
static int sl_sha_file(const wchar_t *path, unsigned char out[32],
                       unsigned long long *size_out)
{
    HANDLE h;
    sl_hash hs;
    unsigned char *buf;
    unsigned long long total = 0;

    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;

    if (!sl_hash_begin(&hs)) { CloseHandle(h); return 0; }
    buf = (unsigned char *) malloc(1u << 20);
    if (buf == NULL) { sl_hash_end(&hs, out); CloseHandle(h); return 0; }

    for (;;) {
        DWORD got = 0;
        if (!ReadFile(h, buf, 1u << 20, &got, NULL)) {
            free(buf); sl_hash_end(&hs, out); CloseHandle(h); return 0;
        }
        if (got == 0) break;
        if (!sl_hash_feed(&hs, buf, got)) {
            free(buf); sl_hash_end(&hs, out); CloseHandle(h); return 0;
        }
        total += got;
    }
    free(buf);
    CloseHandle(h);
    if (!sl_hash_end(&hs, out)) return 0;
    if (size_out != NULL) *size_out = total;
    return 1;
}

static int sl_unhex(const char *s, unsigned char *out, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        int hi, lo, k;
        int v[2];
        for (k = 0; k < 2; k++) {
            char c = s[i * 2 + k];
            if (c >= '0' && c <= '9')      v[k] = c - '0';
            else if (c >= 'a' && c <= 'f') v[k] = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v[k] = c - 'A' + 10;
            else return 0;
        }
        hi = v[0]; lo = v[1];
        out[i] = (unsigned char) ((hi << 4) | lo);
    }
    return 1;
}

/* ------------------------------------------------------------------ paths -- */

static void sl_join(wchar_t *out, size_t cap, const wchar_t *a, const wchar_t *b)
{
    size_t n;

    out[0] = L'\0';
    n = wcslen(a);
    if (n + 2 >= cap) return;
    wcscpy(out, a);
    if (n != 0 && out[n - 1] != L'\\' && out[n - 1] != L'/') {
        out[n++] = L'\\';
        out[n] = L'\0';
    }
    if (n + wcslen(b) + 1 > cap) { out[0] = L'\0'; return; }
    wcscat(out, b);
}

/* Create every missing component of a directory path. Components that already
 * exist fail with ERROR_ALREADY_EXISTS, which is the expected case and not
 * inspected; what matters is whether the file underneath can be created, and
 * the caller reports that. */
static void sl_mkdir_p(const wchar_t *dir)
{
    wchar_t buf[SL_MAXPATH];
    wchar_t *p;

    if (wcslen(dir) + 1 >= sizeof buf / sizeof buf[0]) return;
    wcscpy(buf, dir);
    for (p = buf; *p != L'\0'; p++) {
        if ((*p == L'\\' || *p == L'/') && p != buf) {
            wchar_t saved = *p;
            *p = L'\0';
            /* "C:" is not a creatable directory; skip the bare drive prefix
             * and the leading "\\\\" of a UNC name. */
            if (!(p - buf == 2 && buf[1] == L':') && p - buf > 1)
                CreateDirectoryW(buf, NULL);
            *p = saved;
        }
    }
    CreateDirectoryW(buf, NULL);
}

static void sl_mkdir_for(const wchar_t *file)
{
    wchar_t buf[SL_MAXPATH];
    size_t n;

    if (wcslen(file) + 1 >= sizeof buf / sizeof buf[0]) return;
    wcscpy(buf, file);
    n = wcslen(buf);
    while (n > 0 && buf[n - 1] != L'\\' && buf[n - 1] != L'/') n--;
    if (n == 0) return;
    buf[n - 1] = L'\0';
    sl_mkdir_p(buf);
}

/* ------------------------------------------------------------- resources -- */

static const void *sl_res(const char *name, DWORD *len)
{
    HRSRC   r;
    HGLOBAL g;
    const void *p;

    r = FindResourceA(NULL, name, RT_RCDATA);
    if (r == NULL) return NULL;
    *len = SizeofResource(NULL, r);
    g = LoadResource(NULL, r);
    if (g == NULL) return NULL;
    p = LockResource(g);
    return p;
}

/* -------------------------------------------------------------- manifest -- */

static char *sl_tok(char **p)
{
    char *s = *p;
    char *start;

    while (*s == ' ' || *s == '\t') s++;
    if (*s == '\0') { *p = s; return NULL; }
    start = s;
    while (*s != '\0' && *s != ' ' && *s != '\t') s++;
    if (*s != '\0') { *s = '\0'; s++; }
    *p = s;
    return start;
}

static int sl_widen(const char *in, wchar_t *out, int cap)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, in, -1, out, cap);
    return n > 0;
}

static int sl_manifest_parse(const char *text, DWORD len)
{
    char *copy = (char *) malloc((size_t) len + 1);
    char *line;
    char *save;
    int   ok = 1;

    if (copy == NULL) return 0;
    memcpy(copy, text, len);
    copy[len] = '\0';

    line = copy;
    while (line != NULL && *line != '\0') {
        char *eol = strchr(line, '\n');
        char *cur;
        char *kw;

        if (eol != NULL) { *eol = '\0'; save = eol + 1; } else { save = NULL; }
        /* tolerate CRLF even though the packager writes LF */
        { size_t n = strlen(line); if (n > 0 && line[n - 1] == '\r') line[n - 1] = '\0'; }

        cur = line;
        kw = sl_tok(&cur);
        if (kw == NULL || kw[0] == '#') { line = save; continue; }

        if (strcmp(kw, "id") == 0) {
            char *v = sl_tok(&cur);
            if (v == NULL || strlen(v) >= sizeof g_id) { ok = 0; break; }
            strcpy(g_id, v);
        } else if (strcmp(kw, "file") == 0) {
            char *res  = sl_tok(&cur);
            char *rel  = sl_tok(&cur);
            char *size = sl_tok(&cur);
            char *sha  = sl_tok(&cur);
            sl_file *f;
            if (res == NULL || rel == NULL || size == NULL || sha == NULL
                || g_nfile >= SL_MAXFILES || strlen(res) >= sizeof f->res
                || strlen(sha) != 64) { ok = 0; break; }
            f = &g_file[g_nfile];
            strcpy(f->res, res);
            if (!sl_widen(rel, f->rel, SL_MAXPATH)) { ok = 0; break; }
            f->size = _strtoui64(size, NULL, 10);
            if (!sl_unhex(sha, f->sha, 32)) { ok = 0; break; }
            g_nfile++;
        } else if (strcmp(kw, "launch") == 0) {
            char *v = sl_tok(&cur);
            if (v == NULL || !sl_widen(v, g_launch, SL_MAXPATH)) { ok = 0; break; }
        } else if (strcmp(kw, "cwd") == 0) {
            char *v = sl_tok(&cur);
            if (v == NULL || !sl_widen(v, g_cwd, SL_MAXPATH)) { ok = 0; break; }
        } else if (strcmp(kw, "env") == 0) {
            char *n = sl_tok(&cur);
            char *v = sl_tok(&cur);
            if (n == NULL || v == NULL || g_nenv >= SL_MAXENV) { ok = 0; break; }
            if (!sl_widen(n, g_env[g_nenv].name, 128)) { ok = 0; break; }
            if (!sl_widen(v, g_env[g_nenv].value, SL_MAXPATH * 2)) { ok = 0; break; }
            g_nenv++;
        }
        /* An unknown keyword is ignored on purpose: a newer packager may add
         * one, and refusing to run is a worse answer than skipping a line
         * this build has no use for. */
        line = save;
    }
    free(copy);
    return ok && g_nfile > 0 && g_id[0] != '\0' && g_launch[0] != L'\0';
}

/* Replace every occurrence of {ROOT} with the cache root. */
static void sl_expand(const wchar_t *in, const wchar_t *root, wchar_t *out,
                      size_t cap)
{
    size_t o = 0;
    const wchar_t *p = in;

    out[0] = L'\0';
    while (*p != L'\0') {
        if (p[0] == L'{' && wcsncmp(p, L"{ROOT}", 6) == 0) {
            size_t n = wcslen(root);
            if (o + n + 1 > cap) return;
            wcscpy(out + o, root);
            o += n;
            p += 6;
        } else {
            if (o + 2 > cap) return;
            out[o++] = *p++;
            out[o] = L'\0';
        }
    }
}

/* ------------------------------------------------------------ extraction -- */

/*
 * Write one payload file. The bytes go to <target>.part first, are flushed,
 * closed, verified for length, and only then does MoveFileExW replace the
 * target in one step. A run interrupted anywhere in that sequence leaves the
 * real target either absent or intact - never half written - and the .part
 * file is overwritten by the next attempt.
 */
static int sl_write_file(const wchar_t *target, const void *data, DWORD len)
{
    wchar_t tmp[SL_MAXPATH];
    HANDLE  h;
    const unsigned char *p = (const unsigned char *) data;
    DWORD   left = len;

    if (wcslen(target) + 6 >= sizeof tmp / sizeof tmp[0]) return 0;
    wcscpy(tmp, target);
    wcscat(tmp, L".part");

    sl_mkdir_for(target);

    h = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;

    while (left > 0) {
        DWORD chunk = (left > (1u << 20)) ? (1u << 20) : left;
        DWORD wrote = 0;
        if (!WriteFile(h, p, chunk, &wrote, NULL) || wrote != chunk) {
            CloseHandle(h);
            DeleteFileW(tmp);
            return 0;
        }
        p += chunk;
        left -= chunk;
    }
    /* A short write is otherwise invisible until the next launch fails a hash
     * check, so the size is confirmed while the handle is still open. */
    {
        LARGE_INTEGER end;
        end.QuadPart = 0;
        if (!GetFileSizeEx(h, &end) || (unsigned long long) end.QuadPart != len) {
            CloseHandle(h);
            DeleteFileW(tmp);
            return 0;
        }
    }
    FlushFileBuffers(h);
    if (!CloseHandle(h)) { DeleteFileW(tmp); return 0; }

    if (!MoveFileExW(tmp, target,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp);
        return 0;
    }
    return 1;
}

/* --------------------------------------------------------------- the run -- */

static int sl_localappdata(wchar_t *out, size_t cap)
{
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", out, (DWORD) cap);
    if (n > 0 && n < cap) return 1;
    /* Same ladder the rest of Sightline uses for player data: LOCALAPPDATA,
     * then USERPROFILE\AppData\Local, then TEMP. */
    {
        wchar_t up[SL_MAXPATH];
        n = GetEnvironmentVariableW(L"USERPROFILE", up, SL_MAXPATH);
        if (n > 0 && n < SL_MAXPATH) {
            sl_join(out, cap, up, L"AppData\\Local");
            if (out[0] != L'\0') return 1;
        }
    }
    n = GetEnvironmentVariableW(L"TEMP", out, (DWORD) cap);
    return n > 0 && n < cap;
}

/*
 * Every SL_* variable in this process is removed before the four the demo
 * needs are set, and the child then inherits this cleaned environment.
 *
 * This is the isolation requirement, and it is a sweep rather than a list on
 * purpose: src/ reads over a hundred distinct SL_* names, and any one of them
 * left in a shell - SL_FRAMES, SL_SHOT, SL_BOOT_LEVEL, SL_TRACE_OUT,
 * SL_INPUT, a stale SL_ROM - would turn the showcase into a frame-limited,
 * screenshot-taking, direct-booting, replaying or wrong-ROM run. Enumerating
 * them would be a list to keep in step with the code; the prefix cannot fall
 * out of step.
 */
static int sl_clear_sl_env(void)
{
    wchar_t *block = GetEnvironmentStringsW();
    wchar_t *p;
    wchar_t  names[512][128];
    int      n = 0, i;

    if (block == NULL) return 0;
    for (p = block; *p != L'\0'; p += wcslen(p) + 1) {
        const wchar_t *eq;
        size_t len;
        /* "=C:=C:\..." drive-current-directory entries start with '='. */
        if (p[0] == L'=') continue;
        if (!(p[0] == L'S' || p[0] == L's')) continue;
        if (!(p[1] == L'L' || p[1] == L'l')) continue;
        if (p[2] != L'_') continue;
        eq = wcschr(p, L'=');
        if (eq == NULL) continue;
        len = (size_t) (eq - p);
        if (len == 0 || len >= 128 || n >= 512) continue;
        wmemcpy(names[n], p, len);
        names[n][len] = L'\0';
        n++;
    }
    FreeEnvironmentStringsW(block);

    for (i = 0; i < n; i++) SetEnvironmentVariableW(names[i], NULL);
    return n;
}

int main(void)
{
    wchar_t base[SL_MAXPATH];
    wchar_t root[SL_MAXPATH];
    wchar_t idw[128];
    wchar_t exe[SL_MAXPATH];
    wchar_t cwd[SL_MAXPATH];
    wchar_t cmd[SL_MAXPATH + 4];
    const char *man;
    DWORD manlen = 0;
    int i, extracted = 0, reused = 0, cleared;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    DWORD code = 0;

    {
        DWORD pids[8];
        DWORD np = GetConsoleProcessList(pids, 8);
        g_own_console = (np == 1);
    }

    man = (const char *) sl_res("SL_MANIFEST", &manlen);
    if (man == NULL || manlen == 0)
        return sl_fail(L"this executable carries no payload manifest.");
    if (!sl_sha_open())
        return sl_fail(L"Windows would not open a SHA-256 provider.");
    if (!sl_manifest_parse(man, manlen))
        return sl_fail(L"the payload manifest is malformed.");

    if (!sl_localappdata(base, SL_MAXPATH))
        return sl_fail(L"no LOCALAPPDATA, USERPROFILE or TEMP to unpack into.");

    if (!sl_widen(g_id, idw, 128))
        return sl_fail(L"the payload id is malformed.");
    {
        wchar_t mid[SL_MAXPATH];
        sl_join(mid, SL_MAXPATH, base, L"sightline\\demo\\runtime");
        sl_join(root, SL_MAXPATH, mid, idw);
    }
    if (root[0] == L'\0')
        return sl_fail(L"the unpack path is too long.");

    sl_say(L"SightlineDemo: payload %ls\n", idw);
    sl_say(L"SightlineDemo: runtime  %ls\n", root);

    sl_mkdir_p(root);

    for (i = 0; i < g_nfile; i++) {
        wchar_t path[SL_MAXPATH];
        unsigned char have[32];
        unsigned long long havelen = 0;
        const void *data;
        DWORD len = 0;
        int good;

        sl_join(path, SL_MAXPATH, root, g_file[i].rel);
        if (path[0] == L'\0')
            return sl_fail(L"path too long for %ls", g_file[i].rel);

        good = sl_sha_file(path, have, &havelen)
               && havelen == g_file[i].size
               && memcmp(have, g_file[i].sha, 32) == 0;
        if (good) { reused++; continue; }

        data = sl_res(g_file[i].res, &len);
        if (data == NULL)
            return sl_fail(L"payload resource %hs is missing.", g_file[i].res);
        if ((unsigned long long) len != g_file[i].size)
            return sl_fail(L"payload resource %hs is %lu bytes, manifest says "
                           L"%llu.", g_file[i].res, (unsigned long) len,
                           g_file[i].size);
        {
            unsigned char got[32];
            if (!sl_sha_mem(data, len, got) ||
                memcmp(got, g_file[i].sha, 32) != 0)
                return sl_fail(L"payload resource %hs does not match its "
                               L"recorded hash.", g_file[i].res);
        }
        if (!sl_write_file(path, data, len))
            return sl_fail(L"could not write %ls", path);

        /* Read it back. The write path already checks the length, but the
         * point of a hash-addressed cache is that what is on disk is what was
         * packaged, and the only way to know that is to look. */
        if (!sl_sha_file(path, have, &havelen) || havelen != g_file[i].size
            || memcmp(have, g_file[i].sha, 32) != 0)
            return sl_fail(L"%ls did not verify after extraction.", path);
        extracted++;
    }
    sl_say(L"SightlineDemo: payload  %d file(s) - %d extracted, %d reused and "
           L"verified\n", g_nfile, extracted, reused);

    sl_join(exe, SL_MAXPATH, root, g_launch);
    if (g_cwd[0] != L'\0') sl_join(cwd, SL_MAXPATH, root, g_cwd);
    else                   wcscpy(cwd, root);
    if (exe[0] == L'\0' || cwd[0] == L'\0')
        return sl_fail(L"the launch path is too long.");

    cleared = sl_clear_sl_env();
    sl_say(L"SightlineDemo: env      cleared %d inherited SL_* variable(s)\n",
           cleared);
    for (i = 0; i < g_nenv; i++) {
        wchar_t val[SL_MAXPATH * 2];
        sl_expand(g_env[i].value, root, val, sizeof val / sizeof val[0]);
        if (!SetEnvironmentVariableW(g_env[i].name, val))
            return sl_fail(L"could not set %ls", g_env[i].name);
        sl_say(L"SightlineDemo: env      %ls=%ls\n", g_env[i].name, val);
    }
    sl_say(L"SightlineDemo: cwd      %ls\n", cwd);
    sl_say(L"SightlineDemo: launch   %ls\n", exe);

    /* Hidden LAST, so everything above is readable when there is a terminal
     * to read it and the failure paths above can still raise a dialog. See
     * the console note at the top of this file: the console stays ATTACHED. */
    if (g_own_console) {
        HWND h = GetConsoleWindow();
        if (h != NULL) ShowWindow(h, SW_HIDE);
    }

    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    ZeroMemory(&pi, sizeof pi);

    /* argv[0], quoted. lpApplicationName carries the real path, so this is
     * only what the child sees as its own name. */
    _snwprintf(cmd, sizeof cmd / sizeof cmd[0] - 1, L"\"%ls\"", exe);
    cmd[sizeof cmd / sizeof cmd[0] - 1] = L'\0';

    /*
     * NO CREATION FLAGS. Not CREATE_NEW_CONSOLE, not DETACHED_PROCESS, not
     * CREATE_NO_WINDOW: the core inherits THIS console, which is what
     * `& $exe` from a prompt gives it and what its isatty branches expect.
     * bInheritHandles is TRUE so the standard handles come through as well,
     * which is what makes a redirected launch readable.
     */
    if (!CreateProcessW(exe, cmd, NULL, NULL, TRUE, 0, NULL, cwd, &si, &pi)) {
        DWORD e = GetLastError();
        if (g_own_console) {
            HWND h = GetConsoleWindow();
            if (h != NULL) ShowWindow(h, SW_SHOW);
        }
        return sl_fail(L"could not start %ls (error %lu).", exe,
                       (unsigned long) e);
    }
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    if (!GetExitCodeProcess(pi.hProcess, &code)) code = 1;
    CloseHandle(pi.hProcess);

    return (int) code;
}
