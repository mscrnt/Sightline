/**
 * sl_settings.c - the native settings store (#41). See sl_settings.h for the
 * contract, the file format and where the file lives.
 *
 * WHY src/platform AND NOT src/native. This file is pure host code - getenv,
 * stdio, a directory create and an atomic file replace - and src/platform is
 * the class compiled against the HOST headers. src/native is compiled against
 * the N64 include tree, where <stdlib.h> is a stub with no getenv and
 * <string.h> declares strlen over unsigned char, which is why every src/native
 * file that needs libc hand-declares it. The predecessor of this store (the
 * .style sidecar, sl_style_load / sl_style_save in sl_ultra_shim.c) lived in
 * this class for the same reason. The game-facing half - applying the values
 * through the game's own setters - is src/native/sl_settings_apply.c, which
 * needs the game headers and only ever calls the typed API below.
 *
 * NO FRAMEWORK. One table of rows (name, default, min, max), a parser that
 * reads "key=value" lines, a writer that emits the table. Adding a setting is
 * one enum member and one table row: #42's sprint toggle landed that way
 * (sprint_enabled, the fifth row; no version bump - an older file simply
 * lacks the key and reads as 0). A #43 presentation row landed the same way
 * and was RETIRED unshipped on 2026-09-18 (owner decision: no filtering-only
 * ORIGINAL / MODERN toggle; #43 is parked for a dedicated graphics phase) -
 * a file still carrying that key reads it as an unknown key, ignored.
 *
 * WRITE POLICY. The file is rewritten when, and only when, a set() actually
 * changes a value (or the one-time .style import lands). The write goes to
 * "<path>.tmp" and is then moved over the real file atomically -
 * MoveFileExA(MOVEFILE_REPLACE_EXISTING) on Windows, rename(2) elsewhere - so
 * a crash mid-write leaves the previous file intact rather than a truncated
 * one. Nothing is written at exit: there is nothing dirty to flush.
 *
 * INACTIVE UNTIL init. Before sl_settings_init the store holds defaults, gets
 * return them, sets change memory only and no file is touched. init is called
 * from the player-session path only (sl_eeprom_init_rw), so a trace replay or
 * a headless health run never reads a config and stays bit-identical to
 * before this file existed.
 *
 * SELF-TEST: tools/windows/settingstest.ps1 (compile this file alone with
 * -DSL_SETTINGS_SELFTEST and run it). Every fixture is synthesised in code.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define sl_mkdir(p) _mkdir(p)
#else
#include <sys/stat.h>
#include <unistd.h>
#define sl_mkdir(p) mkdir((p), 0755)
#endif

#include "sl_settings.h"

#define SL_SETTINGS_VERSION 1

struct sl_setting_row {
    const char *key;
    int         dflt;
    int         lo, hi;
};

/* THE TABLE. Index == enum sl_setting. */
static const struct sl_setting_row s_rows[SL_SET_COUNT] = {
    { "look_updown",     0, 0, 1 },  /* 0 reverse, 1 upright (pad stick only) */
    { "aim_control",     0, 0, 1 },  /* 0 hold, 1 toggle */
    { "mouse_invert_y",  0, 0, 1 },  /* 0 off, 1 on (mouse only) */
    { "sprint_enabled",  0, 0, 1 },  /* #42: 0 off (original movement), 1 on */
    { "aspect_ratio",    0, 0, 3 },  /* #45: 0 = 4:3 (the accepted presentation), 1 = 16:9, 2 = 32:9, 3 = 21:9 (append-only ids) */
    { "fov_vertical", 6000, 3598, 7756 }, /* #45: vertical FOV in hundredths, 60.00 = FOV_Y_F; h16 60..110 */
    { "mouse_sensitivity",        SL_MOUSE_SENS_DEFAULT, SL_MOUSE_SENS_MIN, SL_MOUSE_SENS_MAX }, /* #50: percent, 100 = the accepted feel */
    { "scoped_mouse_sensitivity", SL_MOUSE_SENS_DEFAULT, SL_MOUSE_SENS_MIN, SL_MOUSE_SENS_MAX }, /* #50: percent, on top, adjustable scope only */
    { "pad_button_layout", SL_BUTTON_LAYOUT_DEFAULT, 0, SL_BUTTON_LAYOUT_COUNT - 1 }, /* #63: the preset the PAD bindings were seeded from, or CUSTOM */
    { "pad_stick_layout",  SL_STICK_LAYOUT_DEFAULT,  0, SL_STICK_LAYOUT_COUNT - 1 },  /* #63: DEFAULT / SOUTHPAW / LEGACY / LEGACY SOUTHPAW */
    { "pad_look_sensitivity", SL_PAD_LOOK_SENS_DEFAULT, SL_PAD_LOOK_SENS_MIN, SL_PAD_LOOK_SENS_MAX }, /* #51: percent, 100 = the accepted look */
    { "pad_look_deadzone",    SL_PAD_DEADZONE_DEFAULT,  SL_PAD_DEADZONE_MIN,  SL_PAD_DEADZONE_MAX },  /* #51: percent of travel, 15 = the compiled 5000 raw */
    { "pad_move_deadzone",    SL_PAD_DEADZONE_DEFAULT,  SL_PAD_DEADZONE_MIN,  SL_PAD_DEADZONE_MAX },  /* #51: the move pair's, the same default */
    { "crouch_mode",  SL_ACTION_MODE_HOLD, 0, 1 },  /* #56: 0 HOLD (the pre-#56 behaviour), 1 TOGGLE */
    { "sprint_mode",  SL_ACTION_MODE_HOLD, 0, 1 },  /* #56: the same for SPRINT; sprint_enabled stays the gate */
    /* #52 (2026-09-20): the PC display modes. 0 / 0 sizes = not chosen (the
     * launcher's window / the desktop mode); the SDL backend commits what it
     * read back, sl_window.c holds the semantics. */
    { "window_mode",       SL_WINDOW_WINDOWED, 0, SL_WINDOW_MODE_COUNT - 1 }, /* 0 WINDOWED (the accepted launch), 1 BORDERLESS, 2 FULLSCREEN */
    { "window_width",      0, 0, SL_WINDOW_SIZE_MAX },  /* the windowed client size; the height is the authority, the width the aspect's */
    { "window_height",     0, 0, SL_WINDOW_SIZE_MAX },
    { "fullscreen_width",  0, 0, SL_WINDOW_SIZE_MAX },  /* the exclusive mode; 0 / 0 or an unoffered pair = the desktop mode */
    { "fullscreen_height", 0, 0, SL_WINDOW_SIZE_MAX },
    { "vsync",             0, 0, 1 }                    /* the GL swap interval, 0 = the accepted pacing */
    /* RETIRED 2026-09-20 (#63): control_style, pad_button_mode and
     * controller_profile. A file still carrying them reads them as unknown
     * keys - ignored, and dropped on the next write-on-change. */
};

const char *sl_stick_layout_name(int layout)
{
    static const char *const names[SL_STICK_LAYOUT_COUNT] = {
        "DEFAULT", "SOUTHPAW", "LEGACY", "LEGACY SOUTHPAW"
    };
    return (layout >= 0 && layout < SL_STICK_LAYOUT_COUNT) ? names[layout] : "?";
}

static int  s_value[SL_SET_COUNT];
static int  s_have_defaults;         /* s_value holds the table's defaults */
static int  s_active;
static int  s_file_found;            /* did init read an existing file? */
static char s_path[520];

/* The "bind." extension lines (#46), in file order then insertion order. */
struct sl_ext_line {
    char key[SL_SETTINGS_EXT_KEY];
    char val[SL_SETTINGS_EXT_VAL];
};
static struct sl_ext_line s_ext[SL_SETTINGS_EXT_MAX];
static int  s_ext_count;
static int  s_batch_depth;           /* > 0: sets do not write until the end */
static int  s_batch_dirty;           /* a change happened inside the batch */

static int sl_settings_save(void);

/* ------------------------------------------------------------- helpers -- */

static void sl_settings_defaults(void)
{
    int i;
    for (i = 0; i < SL_SET_COUNT; i++)
        s_value[i] = s_rows[i].dflt;
    s_ext_count = 0;
    s_have_defaults = 1;
}

/* Is this a key the extension keeps? The prefix, a length that fits, and
 * nothing that would break the line format. */
static int sl_ext_key_ok(const char *key)
{
    size_t n = strlen(SL_SETTINGS_EXT_PREFIX);
    if (key == NULL || strncmp(key, SL_SETTINGS_EXT_PREFIX, n) != 0) return 0;
    if (strlen(key) <= n || strlen(key) >= SL_SETTINGS_EXT_KEY) return 0;
    if (strchr(key, '=') != NULL || strchr(key, ' ') != NULL) return 0;
    return 1;
}

static int sl_ext_find(const char *key)
{
    int i;
    for (i = 0; i < s_ext_count; i++)
        if (strcmp(s_ext[i].key, key) == 0) return i;
    return -1;
}

/* Store without writing; returns 1 if the held state changed. */
static int sl_ext_store(const char *key, const char *value)
{
    int i = sl_ext_find(key);

    if (value == NULL || value[0] == '\0') {
        if (i < 0) return 0;
        for (; i + 1 < s_ext_count; i++) s_ext[i] = s_ext[i + 1];
        s_ext_count--;
        return 1;
    }
    if (strlen(value) >= SL_SETTINGS_EXT_VAL || strchr(value, '\n') != NULL
        || strchr(value, '\r') != NULL)
        return 0;
    if (i >= 0) {
        if (strcmp(s_ext[i].val, value) == 0) return 0;
        strcpy(s_ext[i].val, value);
        return 1;
    }
    if (s_ext_count >= SL_SETTINGS_EXT_MAX) return 0;
    strcpy(s_ext[s_ext_count].key, key);
    strcpy(s_ext[s_ext_count].val, value);
    s_ext_count++;
    return 1;
}

/* A change happened: write now, or remember to at the end of the batch. */
static void sl_settings_changed(void)
{
    if (!s_active) return;
    if (s_batch_depth > 0) { s_batch_dirty = 1; return; }
    (void) sl_settings_save();
}

/* Before init the table must still answer with the defaults, not with the
 * zero-initialised storage: a get() from the front end on a build that never
 * initialises the store (no writable save) reads the same cold-start values
 * a fresh config would. */
static void sl_settings_touch(void)
{
    if (!s_have_defaults) sl_settings_defaults();
}

/* Trim leading and trailing blanks in place; returns the start. */
static char *sl_trim(char *s)
{
    char *e;
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n'))
        *--e = '\0';
    return s;
}

/* A strict decimal integer: optional sign, digits, nothing else. "1x", "",
 * "on" and "1.0" are all malformed and fall back to the default. */
static int sl_parse_int(const char *s, int *out)
{
    char *end;
    long v;
    if (*s == '\0') return 0;
    errno = 0;
    v = strtol(s, &end, 10);
    if (errno != 0 || *end != '\0') return 0;
    if (v < -2147483647L || v > 2147483647L) return 0;
    *out = (int) v;
    return 1;
}

/* Create every component of the directory holding `file`. Existing
 * components fail harmlessly; only the later fopen decides anything. */
static void sl_settings_mkdir_for(const char *file)
{
    char dir[520];
    char *q;
    size_t n = strlen(file);

    if (n >= sizeof dir) return;
    memcpy(dir, file, n + 1);
    for (q = dir + n; q > dir && *q != '\\' && *q != '/'; q--) ;
    if (q == dir) return;
    *q = '\0';
    for (q = dir; *q != '\0'; q++) {
        if ((*q == '\\' || *q == '/') && q != dir) {
            char saved = *q;
            *q = '\0';
            if (!(q - dir == 2 && dir[1] == ':'))
                (void) sl_mkdir(dir);
            *q = saved;
        }
    }
    (void) sl_mkdir(dir);
}

/* The player-data root, resolved the way every other player file in
 * Sightline resolves it (sl_asset_override.c, sl_demo_save_path, play.ps1). */
static const char *sl_settings_root(char *buf, size_t cap)
{
    const char *e = getenv("LOCALAPPDATA");
    if (e == NULL || e[0] == '\0') {
        const char *u = getenv("USERPROFILE");
        if (u != NULL && u[0] != '\0' && strlen(u) + sizeof "\\AppData\\Local" < cap) {
            strcpy(buf, u);
            strcat(buf, "\\AppData\\Local");
            return buf;
        }
        e = NULL;
    }
    if (e == NULL || e[0] == '\0') e = getenv("TEMP");
    if (e == NULL || e[0] == '\0') e = ".";
    if (strlen(e) + 1 >= cap) return ".";
    strcpy(buf, e);
    return buf;
}

static void sl_settings_resolve_path(void)
{
    const char *v = getenv("SL_CONFIG");
    char root[400];
    const char *base;
#ifdef SL_DEMO_BUILD
    static const char rel[] = "\\sightline\\demo\\config.ini";
#else
    static const char rel[] = "\\sightline\\config.ini";
#endif

    if (v != NULL && v[0] != '\0') {
        if (strlen(v) < sizeof s_path) strcpy(s_path, v);
        return;
    }
    base = sl_settings_root(root, sizeof root);
    if (strlen(base) + sizeof rel >= sizeof s_path) {
        strcpy(s_path, "sightline-config.ini");
        return;
    }
    strcpy(s_path, base);
    strcat(s_path, rel);
}

/* ---------------------------------------------------------------- load -- */

/* Parse one file into the table. Returns 1 if the file was opened. Every
 * malformed line is skipped and counted; a malformed value leaves the
 * setting at its default. */
static int sl_settings_load_file(const char *path, int *bad_lines)
{
    FILE *f = fopen(path, "r");
    char line[256];
    int bad = 0;

    if (f == NULL) return 0;
    while (fgets(line, sizeof line, f) != NULL) {
        char *s = sl_trim(line);
        char *eq;
        int i, v;
        if (*s == '\0' || *s == '#' || *s == ';') continue;
        eq = strchr(s, '=');
        if (eq == NULL) { bad++; continue; }
        *eq = '\0';
        s = sl_trim(s);
        eq = sl_trim(eq + 1);
        if (strcmp(s, "version") == 0) {
            /* A newer file is still read for the keys we know; the version
             * exists so a future format change has something to key on. */
            continue;
        }
        for (i = 0; i < SL_SET_COUNT; i++) {
            if (strcmp(s, s_rows[i].key) != 0) continue;
            if (sl_parse_int(eq, &v) && v >= s_rows[i].lo && v <= s_rows[i].hi)
                s_value[i] = v;
            else
                bad++;              /* default stands */
            break;
        }
        /* i == SL_SET_COUNT: not a scalar. A "bind." line is kept verbatim
         * for the registry (#46) - an over-long or malformed one is counted
         * and dropped; any other unknown key is ignored by design. */
        if (i == SL_SET_COUNT && strncmp(s, SL_SETTINGS_EXT_PREFIX,
                                         strlen(SL_SETTINGS_EXT_PREFIX)) == 0) {
            if (!sl_ext_key_ok(s) || *eq == '\0' || !sl_ext_store(s, eq))
                bad++;
        }
    }
    fclose(f);
    if (bad_lines) *bad_lines = bad;
    return 1;
}

/* ---------------------------------------------------------------- save -- */

static int sl_settings_replace(const char *tmp, const char *dst)
{
#ifdef _WIN32
    return MoveFileExA(tmp, dst, MOVEFILE_REPLACE_EXISTING) ? 0 : -1;
#else
    return rename(tmp, dst);
#endif
}

static int sl_settings_save(void)
{
    char tmp[540];
    FILE *f;
    int i;

    if (!s_active || s_path[0] == '\0') return -1;
    if (strlen(s_path) + 5 >= sizeof tmp) return -1;
    strcpy(tmp, s_path);
    strcat(tmp, ".tmp");

    sl_settings_mkdir_for(s_path);
    f = fopen(tmp, "w");
    if (f == NULL) {
        fprintf(stderr, "sightline settings: cannot write %s\n", tmp);
        return -1;
    }
    fprintf(f, "# sightline settings - the front end's Options menu writes this;\n");
    fprintf(f, "# edit by hand only while the game is closed.\n");
    fprintf(f, "version=%d\n", SL_SETTINGS_VERSION);
    for (i = 0; i < SL_SET_COUNT; i++)
        fprintf(f, "%s=%d\n", s_rows[i].key, s_value[i]);
    /* The "bind." lines (#46) after the scalars: only the player's overrides
     * exist here (a slot at its default has no line), so a fresh config has
     * none and an old one never gains any until a binding is changed. */
    if (s_ext_count > 0)
        fprintf(f, "# bindings - overrides of the compiled defaults, written by the BINDINGS editors\n");
    for (i = 0; i < s_ext_count; i++)
        fprintf(f, "%s=%s\n", s_ext[i].key, s_ext[i].val);
    if (fclose(f) != 0) { remove(tmp); return -1; }
    if (sl_settings_replace(tmp, s_path) != 0) {
        fprintf(stderr, "sightline settings: cannot replace %s\n", s_path);
        remove(tmp);
        return -1;
    }
    fprintf(stderr, "sightline settings: wrote %s\n", s_path);
    return 0;
}

/* ----------------------------------------------------------------- api -- */

void sl_settings_init(void)
{
    int bad = 0;
    int i;

    if (s_active) return;
    sl_settings_defaults();
    sl_settings_resolve_path();
    s_file_found = sl_settings_load_file(s_path, &bad);
    s_active = 1;

    fprintf(stderr, "sightline settings: %s (%s%s)", s_path,
            s_file_found ? "loaded" : "no file, defaults",
            bad ? ", malformed lines ignored" : "");
    for (i = 0; i < SL_SET_COUNT; i++)
        fprintf(stderr, " %s=%d", s_rows[i].key, s_value[i]);
    fprintf(stderr, " bind-lines=%d\n", s_ext_count);
}

int sl_settings_active(void)
{
    return s_active;
}

int sl_settings_get(int id)
{
    if (id < 0 || id >= SL_SET_COUNT) return 0;
    sl_settings_touch();
    return s_value[id];
}

void sl_settings_set(int id, int value)
{
    if (id < 0 || id >= SL_SET_COUNT) return;
    if (value < s_rows[id].lo || value > s_rows[id].hi) return;
    sl_settings_touch();
    if (s_value[id] == value) return;
    s_value[id] = value;
    sl_settings_changed();
}

const char *sl_settings_path(void)
{
    return s_active ? s_path : "";
}

/* ----------------------------------------------------- "bind." lines -- */

const char *sl_settings_ext_get(const char *key)
{
    int i;
    sl_settings_touch();
    if (key == NULL) return NULL;
    i = sl_ext_find(key);
    return i < 0 ? NULL : s_ext[i].val;
}

int sl_settings_ext_set(const char *key, const char *value)
{
    sl_settings_touch();
    if (!sl_ext_key_ok(key)) return 0;
    if (!sl_ext_store(key, value)) return 0;
    sl_settings_changed();
    return 1;
}

int sl_settings_ext_remove_prefix(const char *prefix)
{
    int i, n, removed = 0;
    sl_settings_touch();
    if (prefix == NULL) return 0;
    n = (int) strlen(prefix);
    for (i = 0; i < s_ext_count; ) {
        if (strncmp(s_ext[i].key, prefix, (size_t) n) == 0) {
            int j;
            for (j = i; j + 1 < s_ext_count; j++) s_ext[j] = s_ext[j + 1];
            s_ext_count--;
            removed++;
        } else {
            i++;
        }
    }
    if (removed) sl_settings_changed();
    return removed;
}

void sl_settings_batch_begin(void)
{
    s_batch_depth++;
}

void sl_settings_batch_end(void)
{
    if (s_batch_depth > 0) s_batch_depth--;
    if (s_batch_depth == 0 && s_batch_dirty) {
        s_batch_dirty = 0;
        if (s_active) (void) sl_settings_save();
    }
}

int sl_settings_ext_count(void)
{
    sl_settings_touch();
    return s_ext_count;
}

/* ====================================================================== */
/* Self-test. tools/windows/settingstest.ps1, or by hand:
 *     gcc -m32 -DSL_SETTINGS_SELFTEST src/platform/sl_settings.c \
 *         -o build/win32/settingstest.exe && build/win32/settingstest.exe
 * Runs against a scratch file under TEMP named through SL_CONFIG; never the
 * player's config. */
#ifdef SL_SETTINGS_SELFTEST

static int g_fail, g_checks;
static void ck(int cond, const char *what)
{
    g_checks++;
    if (!cond) { g_fail++; printf("  FAIL  %s\n", what); }
}

static void write_text(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    if (f) { fputs(text, f); fclose(f); }
}

static int read_text(const char *path, char *buf, size_t cap)
{
    FILE *f = fopen(path, "r");
    size_t n;
    if (!f) return 0;
    n = fread(buf, 1, cap - 1, f);
    buf[n] = '\0';
    fclose(f);
    return 1;
}

/* The store is a singleton with static state; the test re-enters init by
 * resetting that state through this one hook (test build only). */
static void reset_store(void)
{
    s_active = 0; s_file_found = 0; s_path[0] = '\0'; s_have_defaults = 0;
    s_ext_count = 0; s_batch_depth = 0; s_batch_dirty = 0;
}

/* How many times the file has been written - counted by mtime-free means:
 * the test rewrites a marker into the file and checks whether it survived. */
static int file_has(const char *path, const char *needle)
{
    char buf[4096];
    return read_text(path, buf, sizeof buf) && strstr(buf, needle) != NULL;
}

int main(void)
{
    char path[512], buf[1024];
    const char *tmpdir = getenv("TEMP");
    if (tmpdir == NULL || tmpdir[0] == '\0') tmpdir = ".";
    sprintf(path, "%s\\sl_settings_selftest\\sub\\config.ini", tmpdir);
    remove(path);

    /* 1. inactive: defaults, sets are memory-only, nothing written */
    ck(!sl_settings_active(), "inactive before init");
    ck(sl_settings_get(SL_SET_LOOK_UPDOWN) == 0, "default look_updown 0 (reverse) before init");
    sl_settings_set(SL_SET_AIM_CONTROL, 1);
    ck(sl_settings_get(SL_SET_AIM_CONTROL) == 1, "inactive set changes memory");
    ck(!read_text(path, buf, sizeof buf), "inactive set writes nothing");
    reset_store();

    /* 2. SL_CONFIG override, no file -> defaults, path resolved, not created */
    {
        char env[600];
        sprintf(env, "SL_CONFIG=%s", path);
        putenv(env);
    }
    sl_settings_init();
    ck(sl_settings_active(), "active after init");
    ck(strcmp(sl_settings_path(), path) == 0, "SL_CONFIG names the file");
    ck(sl_settings_get(SL_SET_LOOK_UPDOWN) == 0
       && sl_settings_get(SL_SET_AIM_CONTROL) == 0 && sl_settings_get(SL_SET_MOUSE_INVERT_Y) == 0
       && sl_settings_get(SL_SET_SPRINT_ENABLED) == 0,
       "missing file = defaults (sprint_enabled 0)");
    ck(!read_text(path, buf, sizeof buf), "init does not create the file");

    /* 3. set: same value -> no write; new value -> file with parent dirs */
    sl_settings_set(SL_SET_AIM_CONTROL, 0);
    ck(!read_text(path, buf, sizeof buf), "unchanged set writes nothing");
    sl_settings_set(SL_SET_AIM_CONTROL, 1);
    ck(read_text(path, buf, sizeof buf), "changed set writes the file (parent dirs created)");
    ck(strstr(buf, "version=1\n") != NULL, "version key present");
    ck(strstr(buf, "aim_control=1\n") != NULL, "aim_control=1 written");
    ck(strstr(buf, "mouse_invert_y=0\n") != NULL, "every key written");
    ck(strstr(buf, "sprint_enabled=0\n") != NULL, "sprint_enabled=0 written by default (#42)");
    ck(strstr(buf, "control_style=") == NULL && strstr(buf, "pad_button_mode=") == NULL
       && strstr(buf, "controller_profile=") == NULL,
       "the three retired keys are never written (#63)");
    sl_settings_set(SL_SET_AIM_CONTROL, 9);
    ck(sl_settings_get(SL_SET_AIM_CONTROL) == 1, "out-of-range set refused");
    sl_settings_set(SL_SET_SPRINT_ENABLED, 2);
    ck(sl_settings_get(SL_SET_SPRINT_ENABLED) == 0, "sprint_enabled out-of-range set refused");
    sl_settings_set(SL_SET_SPRINT_ENABLED, 1);
    ck(read_text(path, buf, sizeof buf) && strstr(buf, "sprint_enabled=1\n"),
       "sprint_enabled=1 written on change");
    sl_settings_set(SL_SET_MOUSE_INVERT_Y, 1);
    sl_settings_set(SL_SET_LOOK_UPDOWN, 1);
    ck(read_text(path, buf, sizeof buf) && strstr(buf, "mouse_invert_y=1\n") && strstr(buf, "look_updown=1\n"),
       "later sets rewrite the file");
    {
        char tmp[540];
        sprintf(tmp, "%s.tmp", path);
        ck(!read_text(tmp, buf, sizeof buf), "no .tmp left behind");
    }

    /* 4. reload: values survive a restart */
    reset_store();
    sl_settings_init();
    ck(sl_settings_get(SL_SET_AIM_CONTROL) == 1 && sl_settings_get(SL_SET_MOUSE_INVERT_Y) == 1
       && sl_settings_get(SL_SET_LOOK_UPDOWN) == 1
       && sl_settings_get(SL_SET_SPRINT_ENABLED) == 1,
       "reload reads back what was written (sprint_enabled 1)");

    /* 5. hostile file: comments, blanks, spaces, unknown keys, malformed and
     *    out-of-range values, a newer version, CRLF line ends */
    write_text(path,
        "# comment\r\n"
        "\r\n"
        "version=7\r\n"
        "  aim_control = 1  \r\n"
        "look_updown=upright\r\n"        /* malformed -> default 0 */
        "mouse_invert_y=5\r\n"           /* out of range -> default 0 */
        "sprint_enabled=on\r\n"          /* malformed -> default 0 (#42) */
        "future_key=whatever\r\n"        /* unknown -> ignored */
        "this line has no equals\r\n"    /* malformed line -> skipped */
        "; another comment\r\n");
    reset_store();
    sl_settings_init();
    ck(sl_settings_get(SL_SET_AIM_CONTROL) == 1, "spaces around key/value accepted");
    ck(sl_settings_get(SL_SET_LOOK_UPDOWN) == 0, "malformed value -> default");
    ck(sl_settings_get(SL_SET_MOUSE_INVERT_Y) == 0, "out-of-range value -> default");
    ck(sl_settings_get(SL_SET_SPRINT_ENABLED) == 0, "malformed sprint_enabled -> 0");
    ck(sl_settings_active(), "hostile file is never fatal");

    /* 5b. a #41-era file WITHOUT the sprint key (backward compatibility, no
     *     version bump): every old key read, sprint off; "sprint_enabled=1"
     *     and "sprint_enabled=2" parse as on / refused. */
    write_text(path,
        "version=1\n"
        "control_style=3\n"
        "look_updown=1\n"
        "aim_control=1\n"
        "mouse_invert_y=1\n");
    reset_store();
    sl_settings_init();
    ck(sl_settings_get(SL_SET_SPRINT_ENABLED) == 0 && sl_settings_get(SL_SET_AIM_CONTROL) == 1
       && sl_settings_get(SL_SET_MOUSE_INVERT_Y) == 1,
       "file without sprint_enabled -> sprint off, other keys intact");
    write_text(path, "version=1\nsprint_enabled=1\n");
    reset_store();
    sl_settings_init();
    ck(sl_settings_get(SL_SET_SPRINT_ENABLED) == 1, "sprint_enabled=1 parsed");
    write_text(path, "version=1\nsprint_enabled=2\n");
    reset_store();
    sl_settings_init();
    ck(sl_settings_get(SL_SET_SPRINT_ENABLED) == 0, "sprint_enabled=2 out of range -> 0");

    /* 6. THE RETIRED KEYS (#63, 2026-09-20): a file written by an earlier
     *    build with control_style / pad_button_mode / controller_profile
     *    loads with every live key intact, is NOT rewritten by the load
     *    (write-on-change only - the file's bytes are untouched), and the
     *    stale lines go on the next write-on-change like any unknown key. */
    write_text(path,
        "version=1\n"
        "control_style=2\n"
        "look_updown=1\n"
        "aim_control=1\n"
        "mouse_invert_y=0\n"
        "sprint_enabled=1\n"
        "pad_button_mode=1\n"
        "aspect_ratio=1\n"
        "fov_vertical=5872\n"
        "mouse_sensitivity=100\n"
        "scoped_mouse_sensitivity=100\n"
        "controller_profile=1\n"
        "# bindings - overrides of the compiled defaults, written by the BINDINGS editors\n"
        "bind.aim.kbm.2=none\n");
    reset_store();
    sl_settings_init();
    ck(sl_settings_get(SL_SET_LOOK_UPDOWN) == 1 && sl_settings_get(SL_SET_AIM_CONTROL) == 1
       && sl_settings_get(SL_SET_SPRINT_ENABLED) == 1 && sl_settings_get(SL_SET_ASPECT_RATIO) == 1
       && sl_settings_get(SL_SET_FOV_VERTICAL) == 5872 && sl_settings_ext_count() == 1,
       "an owner-shaped #63 round-3 file: every live key and the bind line read");
    ck(sl_settings_get(SL_SET_PAD_BUTTON_LAYOUT) == SL_BUTTON_LAYOUT_DEFAULT
       && sl_settings_get(SL_SET_PAD_STICK_LAYOUT) == SL_STICK_LAYOUT_DEFAULT,
       "... the two layout keys absent -> DEFAULT / DEFAULT");
    ck(file_has(path, "control_style=2\n") && file_has(path, "pad_button_mode=1\n")
       && file_has(path, "controller_profile=1\n"),
       "... and the load did NOT rewrite the file (the stale lines are still there)");
    sl_settings_set(SL_SET_LOOK_UPDOWN, 1);
    ck(file_has(path, "controller_profile=1\n"), "an unchanged set still writes nothing");
    sl_settings_set(SL_SET_PAD_STICK_LAYOUT, SL_STICK_LAYOUT_SOUTHPAW);
    ck(!file_has(path, "control_style=") && !file_has(path, "pad_button_mode=")
       && !file_has(path, "controller_profile=") && file_has(path, "pad_stick_layout=1\n")
       && file_has(path, "look_updown=1\n") && file_has(path, "fov_vertical=5872\n")
       && file_has(path, "bind.aim.kbm.2=none\n"),
       "the next write-on-change drops the stale keys and keeps everything live");
    write_text(path, "version=1\ncontrol_style=x\npad_button_mode=\ncontroller_profile=99\naim_control=1\n");
    reset_store();
    sl_settings_init();
    ck(sl_settings_active() && sl_settings_get(SL_SET_AIM_CONTROL) == 1,
       "malformed stale keys are not fatal and never touch a live key");

    /* 7. #46: the "bind." extension */
    remove(path);
    reset_store();
    sl_settings_init();
    ck(sl_settings_ext_count() == 0, "no bind lines on a fresh store");
    ck(sl_settings_ext_get("bind.fire.kbm.1") == NULL, "ext get of an absent key is NULL");
    ck(sl_settings_ext_set("fire.kbm.1", "key:F") == 0, "ext set without the bind. prefix refused");
    ck(sl_settings_ext_set("bind.fire.kbm.1", "key:F") == 1, "ext set stores a bind line");
    ck(file_has(path, "bind.fire.kbm.1=key:F\n"), "bind line written after the scalars");
    ck(sl_settings_ext_set("bind.fire.kbm.1", "key:F") == 0, "ext set of the same value is no change");
    ck(strcmp(sl_settings_ext_get("bind.fire.kbm.1"), "key:F") == 0, "ext get reads it back");
    sl_settings_batch_begin();
    ck(sl_settings_ext_set("bind.fire.kbm.1", "key:G") == 1, "batched set changes memory");
    ck(!file_has(path, "key:G"), "batched set does not write yet");
    ck(sl_settings_ext_set("bind.aim.kbm.2", "mouse:X1") == 1, "second batched set");
    sl_settings_batch_end();
    ck(file_has(path, "bind.fire.kbm.1=key:G\n") && file_has(path, "bind.aim.kbm.2=mouse:X1\n"),
       "batch end writes both in one file");
    ck(sl_settings_ext_set("bind.fire.kbm.1", NULL) == 1, "ext set NULL removes the line");
    ck(!file_has(path, "bind.fire.kbm.1"), "removed line gone from the file");
    ck(sl_settings_ext_count() == 1, "one bind line left");

    /* reload: the bind line survives */
    reset_store();
    sl_settings_init();
    ck(sl_settings_ext_count() == 1 && strcmp(sl_settings_ext_get("bind.aim.kbm.2"), "mouse:X1") == 0,
       "bind line reloaded");

    /* a hostile file: an unknown future bind key is PRESERVED, a malformed
     * bind line (no value / over-long / a space in the key) is dropped, an
     * old file without any bind line loads as before */
    write_text(path,
        "version=1\n"
        "aim_control=1\n"
        "bind.future_action.kbm.1=key:Z\n"
        "bind.fire.kbm.1=\n"
        "bind.fire kbm.1=key:F\n"
        "bind.this_key_is_far_too_long_for_the_extension_table_to_keep=key:F\n"
        "bind.reload.pad.1=pad:X\n");
    reset_store();
    sl_settings_init();
    ck(sl_settings_get(SL_SET_AIM_CONTROL) == 1, "scalars beside bind lines still read");
    ck(sl_settings_ext_count() == 2, "two well-formed bind lines kept, three malformed dropped");
    ck(strcmp(sl_settings_ext_get("bind.future_action.kbm.1"), "key:Z") == 0, "unknown future bind key preserved");
    ck(strcmp(sl_settings_ext_get("bind.reload.pad.1"), "pad:X") == 0, "well-formed bind line read");
    sl_settings_set(SL_SET_LOOK_UPDOWN, 1);
    ck(file_has(path, "bind.future_action.kbm.1=key:Z\n"), "unknown bind key survives a rewrite");
    ck(sl_settings_ext_remove_prefix("bind.") == 2, "remove_prefix removes every bind line");
    ck(sl_settings_ext_count() == 0 && !file_has(path, "bind."), "and the file has none");
    write_text(path, "version=1\nlook_updown=1\naim_control=1\nmouse_invert_y=1\nsprint_enabled=1\n");
    reset_store();
    sl_settings_init();
    ck(sl_settings_get(SL_SET_SPRINT_ENABLED) == 1 && sl_settings_ext_count() == 0,
       "a #42 file loads unchanged: no bind lines");

    /* 8. The mixed case (#46 owner replay, 2026-09-18): a scalar set, then a
     *    bind line written and rewritten, then a reload - the scalar and the
     *    override both survive every rewrite, in both directions. */
    write_text(path, "version=1\nmouse_invert_y=0\n");
    reset_store();
    sl_settings_init();
    ck(sl_settings_get(SL_SET_MOUSE_INVERT_Y) == 0 && sl_settings_get(SL_SET_SPRINT_ENABLED) == 0,
       "mixed: starts mouse_invert_y=0, sprint_enabled=0");
    sl_settings_set(SL_SET_MOUSE_INVERT_Y, 1);
    sl_settings_set(SL_SET_SPRINT_ENABLED, 1);
    ck(file_has(path, "mouse_invert_y=1\n") && file_has(path, "sprint_enabled=1\n"),
       "mixed: the setters wrote 1 and 1");
    ck(sl_settings_ext_set("bind.interact.kbm.1", "key:F") == 1, "mixed: a bind line written after them");
    sl_settings_batch_begin();
    (void) sl_settings_ext_set("bind.fire.kbm.2", "none");
    (void) sl_settings_ext_set("bind.interact.kbm.1", "key:G");
    sl_settings_batch_end();
    ck(file_has(path, "mouse_invert_y=1\n") && file_has(path, "sprint_enabled=1\n")
       && file_has(path, "bind.interact.kbm.1=key:G\n") && file_has(path, "bind.fire.kbm.2=none\n"),
       "mixed: the bind rewrite kept both scalars");
    reset_store();
    sl_settings_init();
    ck(sl_settings_get(SL_SET_MOUSE_INVERT_Y) == 1 && sl_settings_get(SL_SET_SPRINT_ENABLED) == 1
       && sl_settings_ext_count() == 2 && strcmp(sl_settings_ext_get("bind.interact.kbm.1"), "key:G") == 0,
       "mixed: reload reads 1, 1 and both bind lines");
    sl_settings_set(SL_SET_MOUSE_INVERT_Y, 0);
    ck(file_has(path, "mouse_invert_y=0\n") && file_has(path, "sprint_enabled=1\n")
       && file_has(path, "bind.interact.kbm.1=key:G\n"),
       "mixed: writing 0 keeps sprint and the bind lines");
    reset_store();
    sl_settings_init();
    ck(sl_settings_get(SL_SET_MOUSE_INVERT_Y) == 0 && sl_settings_get(SL_SET_SPRINT_ENABLED) == 1
       && sl_settings_ext_count() == 2, "mixed: reload reads 0, sprint 1, the overrides intact");

    /* 9. #45: the aspect_ratio scalar - default 0 (4:3), the three values
     *    persist, malformed / out-of-range / negative read as 0, a file
     *    without the key (every earlier file) reads 0 with the rest intact,
     *    and the row never disturbs the #46 scalars or the bind lines. */
    remove(path);
    reset_store();
    sl_settings_init();
    ck(sl_settings_get(SL_SET_ASPECT_RATIO) == 0, "aspect_ratio default 0 (4:3) on a missing file");
    sl_settings_set(SL_SET_ASPECT_RATIO, 1);
    ck(sl_settings_get(SL_SET_ASPECT_RATIO) == 1 && file_has(path, "aspect_ratio=1\n"),
       "aspect_ratio=1 (16:9) written on change");
    sl_settings_set(SL_SET_ASPECT_RATIO, 2);
    ck(sl_settings_get(SL_SET_ASPECT_RATIO) == 2 && file_has(path, "aspect_ratio=2\n"),
       "aspect_ratio=2 (32:9) written on change");
    sl_settings_set(SL_SET_ASPECT_RATIO, 3);
    ck(sl_settings_get(SL_SET_ASPECT_RATIO) == 3 && file_has(path, "aspect_ratio=3\n"),
       "aspect_ratio=3 (21:9, the appended id) written on change");
    sl_settings_set(SL_SET_ASPECT_RATIO, 4);
    ck(sl_settings_get(SL_SET_ASPECT_RATIO) == 3, "aspect_ratio=4 out of range refused (stays 3)");
    sl_settings_set(SL_SET_ASPECT_RATIO, 2);
    sl_settings_set(SL_SET_ASPECT_RATIO, -1);
    ck(sl_settings_get(SL_SET_ASPECT_RATIO) == 2, "aspect_ratio=-1 refused (stays 2)");
    ck(file_has(path, "sprint_enabled=0\n") && file_has(path, "mouse_invert_y=0\n"),
       "aspect_ratio writes emit the #39 / #42 scalars beside it");
    reset_store();
    sl_settings_init();
    ck(sl_settings_get(SL_SET_ASPECT_RATIO) == 2, "aspect_ratio=2 reloaded");
    sl_settings_set(SL_SET_ASPECT_RATIO, 0);
    ck(file_has(path, "aspect_ratio=0\n"), "aspect_ratio=0 written back");
    write_text(path, "version=1\naim_control=1\nsprint_enabled=1\naspect_ratio=wide\n");
    reset_store();
    sl_settings_init();
    ck(sl_settings_get(SL_SET_ASPECT_RATIO) == 0 && sl_settings_get(SL_SET_SPRINT_ENABLED) == 1
       && sl_settings_get(SL_SET_AIM_CONTROL) == 1, "malformed aspect_ratio -> 0, other keys intact");
    write_text(path, "version=1\naspect_ratio=7\n");
    reset_store();
    sl_settings_init();
    ck(sl_settings_get(SL_SET_ASPECT_RATIO) == 0, "aspect_ratio=7 out of range -> 0");
    write_text(path, "version=1\naspect_ratio=-2\n");
    reset_store();
    sl_settings_init();
    ck(sl_settings_get(SL_SET_ASPECT_RATIO) == 0, "aspect_ratio=-2 out of range -> 0");
    write_text(path, "version=1\naim_control=1\nmouse_invert_y=1\nsprint_enabled=1\n"
                     "bind.interact.kbm.1=key:F\n");
    reset_store();
    sl_settings_init();
    ck(sl_settings_get(SL_SET_ASPECT_RATIO) == 0 && sl_settings_get(SL_SET_MOUSE_INVERT_Y) == 1
       && sl_settings_ext_count() == 1,
       "a #46 file without aspect_ratio reads 4:3 with every other value intact");
    sl_settings_set(SL_SET_ASPECT_RATIO, 1);
    ck(file_has(path, "aspect_ratio=1\n") && file_has(path, "mouse_invert_y=1\n")
       && file_has(path, "bind.interact.kbm.1=key:F\n"),
       "setting the aspect keeps the #46 scalars and the bind line");

    /* 10. #45: fov_vertical - default 6000 (60.00 = FOV_Y_F), the range
     *     3598..7756, malformed / missing -> default, persists, and the
     *     bindings-only RESET (ext_remove_prefix) leaves it alone. */
    remove(path);
    reset_store();
    sl_settings_init();
    ck(sl_settings_get(SL_SET_FOV_VERTICAL) == 6000, "fov_vertical default 6000 on a missing file");
    sl_settings_set(SL_SET_FOV_VERTICAL, 7756);
    ck(sl_settings_get(SL_SET_FOV_VERTICAL) == 7756 && file_has(path, "fov_vertical=7756\n"), "fov_vertical=7756 (the h16 110 bound) written");
    sl_settings_set(SL_SET_FOV_VERTICAL, 7757);
    ck(sl_settings_get(SL_SET_FOV_VERTICAL) == 7756, "fov_vertical above the range refused");
    sl_settings_set(SL_SET_FOV_VERTICAL, 3598);
    ck(sl_settings_get(SL_SET_FOV_VERTICAL) == 3598, "fov_vertical=3598 (the h16 60 bound) accepted");
    sl_settings_set(SL_SET_FOV_VERTICAL, 3597);
    ck(sl_settings_get(SL_SET_FOV_VERTICAL) == 3598, "fov_vertical below the range refused");
    ck(sl_settings_ext_set("bind.fire.kbm.1", "key:F") == 1, "a bind line beside it");
    ck(sl_settings_ext_remove_prefix("bind.") == 1 && sl_settings_get(SL_SET_FOV_VERTICAL) == 3598,
       "RESET DEFAULTS (the bind. removal) leaves fov_vertical alone");
    reset_store();
    sl_settings_init();
    ck(sl_settings_get(SL_SET_FOV_VERTICAL) == 3598, "fov_vertical reloaded");
    write_text(path, "version=1\nfov_vertical=ninety\naspect_ratio=3\n");
    reset_store();
    sl_settings_init();
    ck(sl_settings_get(SL_SET_FOV_VERTICAL) == 6000 && sl_settings_get(SL_SET_ASPECT_RATIO) == 3,
       "malformed fov_vertical -> 6000, aspect_ratio=3 (21:9) read beside it");
    write_text(path, "version=1\nfov_vertical=9000\n");
    reset_store();
    sl_settings_init();
    ck(sl_settings_get(SL_SET_FOV_VERTICAL) == 6000, "fov_vertical=9000 out of range -> 6000");

    /* 11. #50: mouse_sensitivity / scoped_mouse_sensitivity - default 100 on
     *     a missing file and on every earlier file, the range 10..300, the
     *     bounds accepted and the neighbours refused, malformed / zero /
     *     negative -> 100, persistence, and independence from the #39 / #42 /
     *     #45 / #46 rows and the bindings-only RESET. */
    remove(path);
    reset_store();
    sl_settings_init();
    ck(sl_settings_get(SL_SET_MOUSE_SENSITIVITY) == 100 && sl_settings_get(SL_SET_SCOPED_MOUSE_SENSITIVITY) == 100,
       "mouse_sensitivity / scoped default 100 on a missing file");
    sl_settings_set(SL_SET_MOUSE_SENSITIVITY, 50);
    ck(sl_settings_get(SL_SET_MOUSE_SENSITIVITY) == 50 && file_has(path, "mouse_sensitivity=50\n")
       && file_has(path, "scoped_mouse_sensitivity=100\n"),
       "mouse_sensitivity=50 written on change, scoped still 100 beside it");
    sl_settings_set(SL_SET_SCOPED_MOUSE_SENSITIVITY, 200);
    ck(sl_settings_get(SL_SET_SCOPED_MOUSE_SENSITIVITY) == 200 && file_has(path, "scoped_mouse_sensitivity=200\n")
       && file_has(path, "mouse_sensitivity=50\n"),
       "scoped_mouse_sensitivity=200 written, mouse_sensitivity=50 kept");
    sl_settings_set(SL_SET_MOUSE_SENSITIVITY, 10);
    ck(sl_settings_get(SL_SET_MOUSE_SENSITIVITY) == 10, "mouse_sensitivity=10 (the minimum) accepted");
    sl_settings_set(SL_SET_MOUSE_SENSITIVITY, 9);
    ck(sl_settings_get(SL_SET_MOUSE_SENSITIVITY) == 10, "mouse_sensitivity=9 refused (stays 10)");
    sl_settings_set(SL_SET_MOUSE_SENSITIVITY, 0);
    ck(sl_settings_get(SL_SET_MOUSE_SENSITIVITY) == 10, "mouse_sensitivity=0 refused (look can never freeze)");
    sl_settings_set(SL_SET_MOUSE_SENSITIVITY, -100);
    ck(sl_settings_get(SL_SET_MOUSE_SENSITIVITY) == 10, "mouse_sensitivity=-100 refused (the sign is Invert Mouse Y's)");
    sl_settings_set(SL_SET_MOUSE_SENSITIVITY, 300);
    ck(sl_settings_get(SL_SET_MOUSE_SENSITIVITY) == 300, "mouse_sensitivity=300 (the maximum) accepted");
    sl_settings_set(SL_SET_MOUSE_SENSITIVITY, 301);
    ck(sl_settings_get(SL_SET_MOUSE_SENSITIVITY) == 300, "mouse_sensitivity=301 refused (stays 300)");
    sl_settings_set(SL_SET_SCOPED_MOUSE_SENSITIVITY, 301);
    sl_settings_set(SL_SET_SCOPED_MOUSE_SENSITIVITY, 0);
    ck(sl_settings_get(SL_SET_SCOPED_MOUSE_SENSITIVITY) == 200, "scoped 301 / 0 refused (stays 200)");
    sl_settings_set(SL_SET_MOUSE_INVERT_Y, 1);
    sl_settings_set(SL_SET_SPRINT_ENABLED, 1);
    sl_settings_set(SL_SET_FOV_VERTICAL, 7000);
    ck(sl_settings_ext_set("bind.fire.kbm.1", "key:F") == 1, "a bind line beside the #50 rows");
    ck(sl_settings_ext_remove_prefix("bind.") == 1 && sl_settings_get(SL_SET_MOUSE_SENSITIVITY) == 300
       && sl_settings_get(SL_SET_SCOPED_MOUSE_SENSITIVITY) == 200,
       "RESET DEFAULTS (the bind. removal) leaves both sensitivities alone");
    reset_store();
    sl_settings_init();
    ck(sl_settings_get(SL_SET_MOUSE_SENSITIVITY) == 300 && sl_settings_get(SL_SET_SCOPED_MOUSE_SENSITIVITY) == 200
       && sl_settings_get(SL_SET_MOUSE_INVERT_Y) == 1 && sl_settings_get(SL_SET_SPRINT_ENABLED) == 1
       && sl_settings_get(SL_SET_FOV_VERTICAL) == 7000,
       "reload: 300 / 200 beside invert 1, sprint 1, fov 7000");
    write_text(path, "version=1\nmouse_sensitivity=fast\nscoped_mouse_sensitivity=1.5\naspect_ratio=1\n");
    reset_store();
    sl_settings_init();
    ck(sl_settings_get(SL_SET_MOUSE_SENSITIVITY) == 100 && sl_settings_get(SL_SET_SCOPED_MOUSE_SENSITIVITY) == 100
       && sl_settings_get(SL_SET_ASPECT_RATIO) == 1,
       "malformed mouse_sensitivity / scoped -> 100 / 100, aspect_ratio=1 read beside them");
    write_text(path, "version=1\nmouse_sensitivity=0\nscoped_mouse_sensitivity=-50\n");
    reset_store();
    sl_settings_init();
    ck(sl_settings_get(SL_SET_MOUSE_SENSITIVITY) == 100 && sl_settings_get(SL_SET_SCOPED_MOUSE_SENSITIVITY) == 100,
       "mouse_sensitivity=0 / scoped=-50 out of range -> 100 / 100");
    write_text(path, "version=1\nmouse_sensitivity=5000\nscoped_mouse_sensitivity=301\n");
    reset_store();
    sl_settings_init();
    ck(sl_settings_get(SL_SET_MOUSE_SENSITIVITY) == 100 && sl_settings_get(SL_SET_SCOPED_MOUSE_SENSITIVITY) == 100,
       "mouse_sensitivity=5000 / scoped=301 above the range -> 100 / 100");
    write_text(path, "version=1\naim_control=1\nmouse_invert_y=1\nsprint_enabled=1\n"
                     "aspect_ratio=3\nfov_vertical=6500\nbind.interact.kbm.1=key:F\n");
    reset_store();
    sl_settings_init();
    ck(sl_settings_get(SL_SET_MOUSE_SENSITIVITY) == 100 && sl_settings_get(SL_SET_SCOPED_MOUSE_SENSITIVITY) == 100
       && sl_settings_get(SL_SET_MOUSE_INVERT_Y) == 1 && sl_settings_get(SL_SET_ASPECT_RATIO) == 3
       && sl_settings_get(SL_SET_FOV_VERTICAL) == 6500 && sl_settings_ext_count() == 1,
       "a #45 file without the #50 keys reads 100 / 100 with every other value intact (no migration)");
    sl_settings_set(SL_SET_SCOPED_MOUSE_SENSITIVITY, 60);
    ck(file_has(path, "scoped_mouse_sensitivity=60\n") && file_has(path, "mouse_sensitivity=100\n")
       && file_has(path, "mouse_invert_y=1\n") && file_has(path, "fov_vertical=6500\n")
       && file_has(path, "bind.interact.kbm.1=key:F\n"),
       "setting the scoped percent keeps the #39 / #42 / #45 scalars and the bind line");

    /* 12. pad_button_layout / pad_stick_layout (#63, 2026-09-20): DEFAULT on
     *     a missing file and on every earlier file, every id in range written
     *     and read back, the neighbours refused, malformed -> DEFAULT, and
     *     the names of the stick layouts. The registry's seeding of the PAD
     *     slots from a preset is asserted in inputtest, not here. */
    remove(path);
    reset_store();
    sl_settings_init();
    ck(sl_settings_get(SL_SET_PAD_BUTTON_LAYOUT) == SL_BUTTON_LAYOUT_DEFAULT
       && sl_settings_get(SL_SET_PAD_STICK_LAYOUT) == SL_STICK_LAYOUT_DEFAULT,
       "pad_button_layout / pad_stick_layout default DEFAULT on a missing file");
    sl_settings_set(SL_SET_PAD_BUTTON_LAYOUT, SL_BUTTON_LAYOUT_BUMPER);
    ck(sl_settings_get(SL_SET_PAD_BUTTON_LAYOUT) == SL_BUTTON_LAYOUT_BUMPER
       && file_has(path, "pad_button_layout=2\n") && file_has(path, "pad_stick_layout=0\n"),
       "pad_button_layout=2 (BUMPER) written on change, stick layout 0 beside it");
    sl_settings_set(SL_SET_PAD_BUTTON_LAYOUT, SL_BUTTON_LAYOUT_CUSTOM);
    ck(sl_settings_get(SL_SET_PAD_BUTTON_LAYOUT) == SL_BUTTON_LAYOUT_CUSTOM && file_has(path, "pad_button_layout=4\n"),
       "pad_button_layout=4 (CUSTOM) accepted - it is a state the store keeps");
    sl_settings_set(SL_SET_PAD_BUTTON_LAYOUT, SL_BUTTON_LAYOUT_COUNT);
    sl_settings_set(SL_SET_PAD_BUTTON_LAYOUT, -1);
    ck(sl_settings_get(SL_SET_PAD_BUTTON_LAYOUT) == SL_BUTTON_LAYOUT_CUSTOM, "pad_button_layout 5 / -1 refused");
    sl_settings_set(SL_SET_PAD_STICK_LAYOUT, SL_STICK_LAYOUT_LEGACY_SOUTHPAW);
    ck(sl_settings_get(SL_SET_PAD_STICK_LAYOUT) == SL_STICK_LAYOUT_LEGACY_SOUTHPAW && file_has(path, "pad_stick_layout=3\n"),
       "pad_stick_layout=3 (LEGACY SOUTHPAW) written on change");
    sl_settings_set(SL_SET_PAD_STICK_LAYOUT, SL_STICK_LAYOUT_COUNT);
    sl_settings_set(SL_SET_PAD_STICK_LAYOUT, -1);
    ck(sl_settings_get(SL_SET_PAD_STICK_LAYOUT) == SL_STICK_LAYOUT_LEGACY_SOUTHPAW, "pad_stick_layout 4 / -1 refused");
    reset_store();
    sl_settings_init();
    ck(sl_settings_get(SL_SET_PAD_BUTTON_LAYOUT) == SL_BUTTON_LAYOUT_CUSTOM
       && sl_settings_get(SL_SET_PAD_STICK_LAYOUT) == SL_STICK_LAYOUT_LEGACY_SOUTHPAW,
       "reload: 4 / 3 persist");
    write_text(path, "version=1\npad_button_layout=bumper\npad_stick_layout=southpaw\nmouse_sensitivity=50\n");
    reset_store();
    sl_settings_init();
    ck(sl_settings_get(SL_SET_PAD_BUTTON_LAYOUT) == SL_BUTTON_LAYOUT_DEFAULT
       && sl_settings_get(SL_SET_PAD_STICK_LAYOUT) == SL_STICK_LAYOUT_DEFAULT
       && sl_settings_get(SL_SET_MOUSE_SENSITIVITY) == 50,
       "malformed layouts -> DEFAULT / DEFAULT, mouse_sensitivity=50 read beside them");
    write_text(path, "version=1\npad_button_layout=9\npad_stick_layout=-3\n");
    reset_store();
    sl_settings_init();
    ck(sl_settings_get(SL_SET_PAD_BUTTON_LAYOUT) == SL_BUTTON_LAYOUT_DEFAULT
       && sl_settings_get(SL_SET_PAD_STICK_LAYOUT) == SL_STICK_LAYOUT_DEFAULT,
       "out-of-range layouts -> DEFAULT / DEFAULT");
    ck(strcmp(sl_stick_layout_name(SL_STICK_LAYOUT_DEFAULT), "DEFAULT") == 0
       && strcmp(sl_stick_layout_name(SL_STICK_LAYOUT_SOUTHPAW), "SOUTHPAW") == 0
       && strcmp(sl_stick_layout_name(SL_STICK_LAYOUT_LEGACY), "LEGACY") == 0
       && strcmp(sl_stick_layout_name(SL_STICK_LAYOUT_LEGACY_SOUTHPAW), "LEGACY SOUTHPAW") == 0
       && strcmp(sl_stick_layout_name(SL_STICK_LAYOUT_COUNT), "?") == 0,
       "the four stick layout names, and ? out of range");
    ck(sl_settings_ext_set("bind.fire.pad.1", "pad:B") == 1
       && sl_settings_ext_remove_prefix("bind.") == 1
       && sl_settings_get(SL_SET_PAD_STICK_LAYOUT) == SL_STICK_LAYOUT_DEFAULT,
       "the bind. removal alone leaves the stick layout alone (the store side; the registry resets the button layout itself)");

    /* 13. pad_look_sensitivity / pad_look_deadzone / pad_move_deadzone (#51,
     *     2026-09-20): 100 / 15 / 15 on a missing file (the accepted feel:
     *     the compiled 5000-unit deadzone is exactly 15), written on change
     *     beside the layouts and the mouse percents, the bounds refused, a
     *     malformed or out-of-range line -> the default, and none of the
     *     three touched by a mouse or layout write. */
    remove(path);
    reset_store();
    sl_settings_init();
    ck(sl_settings_get(SL_SET_PAD_LOOK_SENSITIVITY) == 100 && sl_settings_get(SL_SET_PAD_LOOK_DEADZONE) == 15
       && sl_settings_get(SL_SET_PAD_MOVE_DEADZONE) == 15,
       "pad_look_sensitivity / pad_look_deadzone / pad_move_deadzone default 100 / 15 / 15 on a missing file");
    ck(SL_PAD_DEADZONE_DEFAULT * 5000 / 15 == 5000 && SL_PAD_LOOK_SENS_DEFAULT == 100,
       "the defaults name the compiled constants (15 -> 5000 raw exactly, 100 = x1.0)");
    sl_settings_set(SL_SET_PAD_LOOK_SENSITIVITY, 150);
    ck(sl_settings_get(SL_SET_PAD_LOOK_SENSITIVITY) == 150 && file_has(path, "pad_look_sensitivity=150\n")
       && file_has(path, "pad_look_deadzone=15\n") && file_has(path, "pad_move_deadzone=15\n")
       && file_has(path, "pad_stick_layout=0\n") && file_has(path, "mouse_sensitivity=100\n"),
       "pad_look_sensitivity=150 written on change, the two deadzones at 15 beside it, layouts and mouse percents kept");
    sl_settings_set(SL_SET_PAD_LOOK_DEADZONE, 25);
    sl_settings_set(SL_SET_PAD_MOVE_DEADZONE, 3);
    ck(sl_settings_get(SL_SET_PAD_LOOK_DEADZONE) == 25 && sl_settings_get(SL_SET_PAD_MOVE_DEADZONE) == 3
       && file_has(path, "pad_look_deadzone=25\n") && file_has(path, "pad_move_deadzone=3\n")
       && file_has(path, "pad_look_sensitivity=150\n"),
       "pad_look_deadzone=25 / pad_move_deadzone=3 written, the sensitivity kept");
    sl_settings_set(SL_SET_PAD_LOOK_SENSITIVITY, 25);
    ck(sl_settings_get(SL_SET_PAD_LOOK_SENSITIVITY) == 25, "pad_look_sensitivity=25 (the minimum) accepted");
    sl_settings_set(SL_SET_PAD_LOOK_SENSITIVITY, 24);
    sl_settings_set(SL_SET_PAD_LOOK_SENSITIVITY, 0);
    sl_settings_set(SL_SET_PAD_LOOK_SENSITIVITY, -100);
    ck(sl_settings_get(SL_SET_PAD_LOOK_SENSITIVITY) == 25, "pad_look_sensitivity 24 / 0 / -100 refused (look can never freeze or flip)");
    sl_settings_set(SL_SET_PAD_LOOK_SENSITIVITY, 200);
    ck(sl_settings_get(SL_SET_PAD_LOOK_SENSITIVITY) == 200, "pad_look_sensitivity=200 (the maximum) accepted");
    sl_settings_set(SL_SET_PAD_LOOK_SENSITIVITY, 201);
    ck(sl_settings_get(SL_SET_PAD_LOOK_SENSITIVITY) == 200, "pad_look_sensitivity=201 refused (stays 200)");
    sl_settings_set(SL_SET_PAD_LOOK_DEADZONE, 0);
    sl_settings_set(SL_SET_PAD_MOVE_DEADZONE, 40);
    ck(sl_settings_get(SL_SET_PAD_LOOK_DEADZONE) == 0 && sl_settings_get(SL_SET_PAD_MOVE_DEADZONE) == 40,
       "deadzone 0 (no deadzone) and 40 (the maximum) accepted");
    sl_settings_set(SL_SET_PAD_LOOK_DEADZONE, -1);
    sl_settings_set(SL_SET_PAD_MOVE_DEADZONE, 41);
    ck(sl_settings_get(SL_SET_PAD_LOOK_DEADZONE) == 0 && sl_settings_get(SL_SET_PAD_MOVE_DEADZONE) == 40,
       "deadzone -1 / 41 refused");
    reset_store();
    sl_settings_init();
    ck(sl_settings_get(SL_SET_PAD_LOOK_SENSITIVITY) == 200 && sl_settings_get(SL_SET_PAD_LOOK_DEADZONE) == 0
       && sl_settings_get(SL_SET_PAD_MOVE_DEADZONE) == 40,
       "reload: 200 / 0 / 40 read back from the file");
    write_text(path, "version=1\npad_look_sensitivity=fast\npad_look_deadzone=1.5\npad_move_deadzone=\nmouse_sensitivity=50\npad_stick_layout=1\n");
    reset_store();
    sl_settings_init();
    ck(sl_settings_get(SL_SET_PAD_LOOK_SENSITIVITY) == 100 && sl_settings_get(SL_SET_PAD_LOOK_DEADZONE) == 15
       && sl_settings_get(SL_SET_PAD_MOVE_DEADZONE) == 15 && sl_settings_get(SL_SET_MOUSE_SENSITIVITY) == 50
       && sl_settings_get(SL_SET_PAD_STICK_LAYOUT) == SL_STICK_LAYOUT_SOUTHPAW,
       "malformed pad_look_sensitivity / deadzones -> 100 / 15 / 15, mouse_sensitivity=50 and pad_stick_layout=1 read beside them");
    write_text(path, "version=1\npad_look_sensitivity=0\npad_look_deadzone=41\npad_move_deadzone=-5\n");
    reset_store();
    sl_settings_init();
    ck(sl_settings_get(SL_SET_PAD_LOOK_SENSITIVITY) == 100 && sl_settings_get(SL_SET_PAD_LOOK_DEADZONE) == 15
       && sl_settings_get(SL_SET_PAD_MOVE_DEADZONE) == 15,
       "out-of-range pad_look_sensitivity=0 / deadzone 41 / -5 -> 100 / 15 / 15");
    write_text(path, "version=1\npad_look_sensitivity=5000\npad_look_deadzone=100\n");
    reset_store();
    sl_settings_init();
    ck(sl_settings_get(SL_SET_PAD_LOOK_SENSITIVITY) == 100 && sl_settings_get(SL_SET_PAD_LOOK_DEADZONE) == 15,
       "pad_look_sensitivity=5000 / pad_look_deadzone=100 above the range -> 100 / 15");
    /* An owner-shaped #63 round-6 file (no #51 keys at all) reads the three
     * defaults and is not rewritten by the load. */
    write_text(path,
        "version=1\nlook_updown=0\naim_control=0\nmouse_invert_y=0\nsprint_enabled=1\naspect_ratio=1\n"
        "fov_vertical=6000\nmouse_sensitivity=100\nscoped_mouse_sensitivity=100\npad_button_layout=0\npad_stick_layout=1\n"
        "bind.fire.pad.1=pad:RT\n");
    reset_store();
    sl_settings_init();
    ck(sl_settings_get(SL_SET_PAD_LOOK_SENSITIVITY) == 100 && sl_settings_get(SL_SET_PAD_LOOK_DEADZONE) == 15
       && sl_settings_get(SL_SET_PAD_MOVE_DEADZONE) == 15 && sl_settings_get(SL_SET_PAD_STICK_LAYOUT) == 1
       && sl_settings_get(SL_SET_SPRINT_ENABLED) == 1 && sl_settings_ext_count() == 1
       && !file_has(path, "pad_look_sensitivity="),
       "a pre-#51 file: the three read as defaults, everything else read, the file not rewritten");
    sl_settings_set(SL_SET_MOUSE_SENSITIVITY, 60);
    ck(file_has(path, "pad_look_sensitivity=100\n") && file_has(path, "pad_look_deadzone=15\n")
       && file_has(path, "pad_move_deadzone=15\n") && file_has(path, "pad_stick_layout=1\n")
       && file_has(path, "bind.fire.pad.1=pad:RT\n") && file_has(path, "sprint_enabled=1\n"),
       "the next write-on-change (a mouse set) adds the three at their defaults and keeps every other line");

    /* 14. CROUCH MODE / SPRINT MODE (#56, 2026-09-20): HOLD (0) on a missing
     *     file, written on change beside sprint_enabled and the #51 rows, 1
     *     accepted and 2 / -1 refused, reload, a malformed or out-of-range
     *     line -> HOLD, a pre-#56 owner-shaped file read as HOLD and not
     *     rewritten, and the MIXED config (both modes TOGGLE, sprint on, a
     *     bind. override, a non-default pad sensitivity, invert y) preserved
     *     across a reload and an unrelated rewrite. */
    remove(path);
    reset_store();
    sl_settings_init();
    ck(sl_settings_get(SL_SET_CROUCH_MODE) == SL_ACTION_MODE_HOLD && sl_settings_get(SL_SET_SPRINT_MODE) == SL_ACTION_MODE_HOLD,
       "crouch_mode / sprint_mode default HOLD (0) on a missing file");
    ck(SL_ACTION_MODE_HOLD == 0 && SL_ACTION_MODE_TOGGLE == 1, "HOLD is 0, TOGGLE is 1 (the aim_control convention)");
    sl_settings_set(SL_SET_CROUCH_MODE, SL_ACTION_MODE_TOGGLE);
    ck(sl_settings_get(SL_SET_CROUCH_MODE) == 1 && file_has(path, "crouch_mode=1\n") && file_has(path, "sprint_mode=0\n")
       && file_has(path, "sprint_enabled=0\n") && file_has(path, "pad_move_deadzone=15\n"),
       "crouch_mode=1 written on change, sprint_mode=0 beside it, sprint_enabled and the #51 rows kept");
    sl_settings_set(SL_SET_SPRINT_MODE, SL_ACTION_MODE_TOGGLE);
    ck(sl_settings_get(SL_SET_SPRINT_MODE) == 1 && file_has(path, "sprint_mode=1\n") && file_has(path, "crouch_mode=1\n"),
       "sprint_mode=1 written, crouch_mode kept");
    sl_settings_set(SL_SET_CROUCH_MODE, 2);
    sl_settings_set(SL_SET_SPRINT_MODE, -1);
    ck(sl_settings_get(SL_SET_CROUCH_MODE) == 1 && sl_settings_get(SL_SET_SPRINT_MODE) == 1, "crouch_mode=2 / sprint_mode=-1 refused");
    reset_store();
    sl_settings_init();
    ck(sl_settings_get(SL_SET_CROUCH_MODE) == 1 && sl_settings_get(SL_SET_SPRINT_MODE) == 1, "reload: both modes read back TOGGLE");
    write_text(path, "version=1\ncrouch_mode=toggle\nsprint_mode=\nsprint_enabled=1\n");
    reset_store();
    sl_settings_init();
    ck(sl_settings_get(SL_SET_CROUCH_MODE) == 0 && sl_settings_get(SL_SET_SPRINT_MODE) == 0 && sl_settings_get(SL_SET_SPRINT_ENABLED) == 1,
       "malformed crouch_mode=toggle / empty sprint_mode -> HOLD, sprint_enabled=1 read beside them");
    write_text(path, "version=1\ncrouch_mode=2\nsprint_mode=7\n");
    reset_store();
    sl_settings_init();
    ck(sl_settings_get(SL_SET_CROUCH_MODE) == 0 && sl_settings_get(SL_SET_SPRINT_MODE) == 0, "out-of-range crouch_mode=2 / sprint_mode=7 -> HOLD");
    /* A pre-#56 owner-shaped file (the #51 keys, no mode keys) reads HOLD
     * and is not rewritten by the load. */
    write_text(path,
        "version=1\nlook_updown=0\naim_control=0\nmouse_invert_y=0\nsprint_enabled=1\naspect_ratio=0\n"
        "fov_vertical=5872\nmouse_sensitivity=100\nscoped_mouse_sensitivity=100\npad_button_layout=0\npad_stick_layout=1\n"
        "pad_look_sensitivity=185\npad_look_deadzone=13\npad_move_deadzone=16\nbind.aim.kbm.2=none\n");
    reset_store();
    sl_settings_init();
    ck(sl_settings_get(SL_SET_CROUCH_MODE) == 0 && sl_settings_get(SL_SET_SPRINT_MODE) == 0
       && sl_settings_get(SL_SET_SPRINT_ENABLED) == 1 && sl_settings_get(SL_SET_PAD_LOOK_SENSITIVITY) == 185
       && sl_settings_ext_count() == 1 && !file_has(path, "crouch_mode="),
       "a pre-#56 file: both modes read HOLD, everything else read, the file not rewritten");
    sl_settings_set(SL_SET_SPRINT_MODE, 1);
    ck(file_has(path, "crouch_mode=0\n") && file_has(path, "sprint_mode=1\n") && file_has(path, "pad_look_sensitivity=185\n")
       && file_has(path, "bind.aim.kbm.2=none\n") && file_has(path, "sprint_enabled=1\n") && file_has(path, "pad_stick_layout=1\n"),
       "the next write-on-change (sprint_mode) adds both mode lines and keeps every other line");
    /* THE MIXED CONFIG: both modes TOGGLE, sprint on, a bind. override, a
     * non-default pad sensitivity and invert y - reloaded intact, and an
     * unrelated rewrite (a mouse set) keeps every one of them. */
    write_text(path,
        "version=1\nmouse_invert_y=1\nsprint_enabled=1\npad_look_sensitivity=150\ncrouch_mode=1\nsprint_mode=1\n"
        "bind.crouch.kbm.1=key:C\n");
    reset_store();
    sl_settings_init();
    ck(sl_settings_get(SL_SET_CROUCH_MODE) == 1 && sl_settings_get(SL_SET_SPRINT_MODE) == 1
       && sl_settings_get(SL_SET_SPRINT_ENABLED) == 1 && sl_settings_get(SL_SET_PAD_LOOK_SENSITIVITY) == 150
       && sl_settings_get(SL_SET_MOUSE_INVERT_Y) == 1 && sl_settings_ext_count() == 1
       && strcmp(sl_settings_ext_get("bind.crouch.kbm.1"), "key:C") == 0,
       "mixed config: crouch_mode=1 sprint_mode=1 sprint_enabled=1 pad_look_sensitivity=150 mouse_invert_y=1 bind.crouch.kbm.1=key:C all read");
    sl_settings_set(SL_SET_MOUSE_SENSITIVITY, 70);
    reset_store();
    sl_settings_init();
    ck(sl_settings_get(SL_SET_CROUCH_MODE) == 1 && sl_settings_get(SL_SET_SPRINT_MODE) == 1
       && sl_settings_get(SL_SET_SPRINT_ENABLED) == 1 && sl_settings_get(SL_SET_PAD_LOOK_SENSITIVITY) == 150
       && sl_settings_get(SL_SET_MOUSE_INVERT_Y) == 1 && sl_settings_get(SL_SET_MOUSE_SENSITIVITY) == 70
       && sl_settings_ext_count() == 1 && strcmp(sl_settings_ext_get("bind.crouch.kbm.1"), "key:C") == 0
       && file_has(path, "crouch_mode=1\n") && file_has(path, "sprint_mode=1\n") && file_has(path, "bind.crouch.kbm.1=key:C\n"),
       "mixed config survives an unrelated rewrite (mouse_sensitivity=70) and a reload");

    /* 15. PC DISPLAY MODES (#52, 2026-09-20): the six rows default to
     *     WINDOWED / 0x0 / 0x0 / vsync off on a missing file (= the accepted
     *     launch: the launcher's window), are written on change beside the
     *     rest, refuse an out-of-range mode / size / vsync, reload, read a
     *     malformed or out-of-range line as the default, and a pre-#52
     *     owner-shaped file reads the defaults without being rewritten. */
    remove(path);
    reset_store();
    sl_settings_init();
    ck(sl_settings_get(SL_SET_WINDOW_MODE) == SL_WINDOW_WINDOWED && sl_settings_get(SL_SET_WINDOW_WIDTH) == 0
       && sl_settings_get(SL_SET_WINDOW_HEIGHT) == 0 && sl_settings_get(SL_SET_FULLSCREEN_WIDTH) == 0
       && sl_settings_get(SL_SET_FULLSCREEN_HEIGHT) == 0 && sl_settings_get(SL_SET_VSYNC) == 0,
       "window_mode WINDOWED, sizes 0x0 (not chosen), vsync 0 on a missing file");
    ck(SL_WINDOW_WINDOWED == 0 && SL_WINDOW_BORDERLESS == 1 && SL_WINDOW_FULLSCREEN == 2, "WINDOWED 0, BORDERLESS 1, FULLSCREEN 2");
    sl_settings_set(SL_SET_WINDOW_MODE, SL_WINDOW_BORDERLESS);
    ck(sl_settings_get(SL_SET_WINDOW_MODE) == 1 && file_has(path, "window_mode=1\n") && file_has(path, "window_width=0\n")
       && file_has(path, "fullscreen_height=0\n") && file_has(path, "vsync=0\n") && file_has(path, "crouch_mode=0\n"),
       "window_mode=1 written on change, the five other #52 rows at their defaults beside it, the #56 rows beside them");
    sl_settings_batch_begin();
    sl_settings_set(SL_SET_WINDOW_WIDTH, 1280);
    sl_settings_set(SL_SET_WINDOW_HEIGHT, 720);
    sl_settings_batch_end();
    sl_settings_set(SL_SET_FULLSCREEN_WIDTH, 1920);
    sl_settings_set(SL_SET_FULLSCREEN_HEIGHT, 1080);
    sl_settings_set(SL_SET_VSYNC, 1);
    ck(file_has(path, "window_width=1280\n") && file_has(path, "window_height=720\n") && file_has(path, "fullscreen_width=1920\n")
       && file_has(path, "fullscreen_height=1080\n") && file_has(path, "vsync=1\n"),
       "1280x720 windowed, 1920x1080 fullscreen and vsync=1 written");
    sl_settings_set(SL_SET_WINDOW_MODE, 3);
    sl_settings_set(SL_SET_WINDOW_MODE, -1);
    sl_settings_set(SL_SET_WINDOW_WIDTH, SL_WINDOW_SIZE_MAX + 1);
    sl_settings_set(SL_SET_WINDOW_HEIGHT, -5);
    sl_settings_set(SL_SET_VSYNC, 2);
    ck(sl_settings_get(SL_SET_WINDOW_MODE) == 1 && sl_settings_get(SL_SET_WINDOW_WIDTH) == 1280
       && sl_settings_get(SL_SET_WINDOW_HEIGHT) == 720 && sl_settings_get(SL_SET_VSYNC) == 1,
       "window_mode 3 / -1, window_width 16385, window_height -5, vsync 2 refused");
    reset_store();
    sl_settings_init();
    ck(sl_settings_get(SL_SET_WINDOW_MODE) == 1 && sl_settings_get(SL_SET_WINDOW_WIDTH) == 1280
       && sl_settings_get(SL_SET_WINDOW_HEIGHT) == 720 && sl_settings_get(SL_SET_FULLSCREEN_WIDTH) == 1920
       && sl_settings_get(SL_SET_FULLSCREEN_HEIGHT) == 1080 && sl_settings_get(SL_SET_VSYNC) == 1,
       "reload: BORDERLESS, 1280x720, 1920x1080, vsync on read back");
    write_text(path, "version=1\nwindow_mode=fullscreen\nwindow_width=\nwindow_height=abc\nfullscreen_width=1920x1080\nvsync=on\nsprint_enabled=1\n");
    reset_store();
    sl_settings_init();
    ck(sl_settings_get(SL_SET_WINDOW_MODE) == 0 && sl_settings_get(SL_SET_WINDOW_WIDTH) == 0 && sl_settings_get(SL_SET_WINDOW_HEIGHT) == 0
       && sl_settings_get(SL_SET_FULLSCREEN_WIDTH) == 0 && sl_settings_get(SL_SET_VSYNC) == 0 && sl_settings_get(SL_SET_SPRINT_ENABLED) == 1,
       "malformed window_mode=fullscreen / empty width / height=abc / 1920x1080 in one key / vsync=on -> the defaults, sprint_enabled=1 read beside them");
    write_text(path, "version=1\nwindow_mode=7\nwindow_width=99999\nwindow_height=-1\nfullscreen_width=16385\nfullscreen_height=7777\nvsync=-1\n");
    reset_store();
    sl_settings_init();
    ck(sl_settings_get(SL_SET_WINDOW_MODE) == 0 && sl_settings_get(SL_SET_WINDOW_WIDTH) == 0 && sl_settings_get(SL_SET_WINDOW_HEIGHT) == 0
       && sl_settings_get(SL_SET_FULLSCREEN_WIDTH) == 0 && sl_settings_get(SL_SET_FULLSCREEN_HEIGHT) == 7777 && sl_settings_get(SL_SET_VSYNC) == 0,
       "out-of-range mode 7 / 99999 / -1 / 16385 / vsync -1 -> the defaults; 7777 is in range and is the display layer's to refuse");
    /* A pre-#52 owner-shaped file (the #56 keys, no display keys) reads the
     * defaults and is not rewritten by the load. */
    write_text(path,
        "version=1\nlook_updown=0\naim_control=0\nmouse_invert_y=0\nsprint_enabled=1\naspect_ratio=0\n"
        "fov_vertical=5872\nmouse_sensitivity=100\nscoped_mouse_sensitivity=100\npad_button_layout=0\npad_stick_layout=0\n"
        "pad_look_sensitivity=100\npad_look_deadzone=15\npad_move_deadzone=15\ncrouch_mode=1\nsprint_mode=1\nbind.aim.kbm.2=none\n");
    reset_store();
    sl_settings_init();
    ck(sl_settings_get(SL_SET_WINDOW_MODE) == 0 && sl_settings_get(SL_SET_WINDOW_WIDTH) == 0 && sl_settings_get(SL_SET_WINDOW_HEIGHT) == 0
       && sl_settings_get(SL_SET_VSYNC) == 0 && sl_settings_get(SL_SET_CROUCH_MODE) == 1 && sl_settings_get(SL_SET_FOV_VERTICAL) == 5872
       && sl_settings_ext_count() == 1 && !file_has(path, "window_mode="),
       "a pre-#52 file: WINDOWED / not chosen / vsync off, everything else read, the file not rewritten");
    sl_settings_set(SL_SET_VSYNC, 1);
    ck(file_has(path, "window_mode=0\n") && file_has(path, "window_width=0\n") && file_has(path, "window_height=0\n")
       && file_has(path, "fullscreen_width=0\n") && file_has(path, "fullscreen_height=0\n") && file_has(path, "vsync=1\n")
       && file_has(path, "crouch_mode=1\n") && file_has(path, "fov_vertical=5872\n") && file_has(path, "bind.aim.kbm.2=none\n"),
       "the next write-on-change (vsync) adds the six display lines and keeps every other line");

    remove(path);
    printf("sl_settings selftest: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
#endif /* SL_SETTINGS_SELFTEST */
