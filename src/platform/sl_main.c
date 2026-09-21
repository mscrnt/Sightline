#define _GNU_SOURCE
/**
 * sl_main.c - native entry, and the crash/hang diagnostic system.
 *
 * WHY THIS IS SELF-SUFFICIENT. The obvious answer to "capture crashes" is core
 * dumps, and on this box it does not work: core_pattern pipes to WSL's own
 * handler, nothing lands in /var/lib/systemd/coredump or /var/crash, and
 * changing it needs root. Measured, not assumed. So the process has to report
 * on itself.
 *
 * THREE FAILURE MODES, and the reason each is covered:
 *
 *   CRASH   SIGSEGV. Handled below. Was previously _exit(5), which threw the
 *           evidence away - that is why no core ever appeared.
 *   HANG    The one that actually bit. The game's own allocator spins on
 *           `while (1);` when a pool is exhausted (memp.c), so a hang is a
 *           NORMAL failure here, not an exotic one. A wall-clock deadline
 *           cannot tell a hang from a long session, so the watchdog watches
 *           FRAME PROGRESS instead and only fires when the pump has stalled.
 *   SCRIBBLE  Memory corruption far from its symptom. Not signals at all -
 *           that is the SL_ASAN build (sl_asan.h).
 *
 * THE PTRACE LINE IS THE IMPORTANT ONE. With ptrace_scope=1 - the default here
 * - only a parent may attach, so a hung game window cannot be inspected after
 * the fact by anyone. PR_SET_PTRACER_ANY lifts that for this process, so
 * `gdb -p <pid>` works on a stuck window from any shell. One syscall at
 * startup, zero cost, and it is the difference between diagnosing a hang and
 * being told "the window is still open" with nothing to show for it.
 *
 * Env:
 *   SL_HANG_SECONDS=n  stalled frames before the watchdog fires (default 30,
 *                      0 disables). "Stalled" means sl_frame did not move.
 *   SL_CRASH_LOG=path  where the report is written (default
 *                      /tmp/sightline-crash-<pid>.txt). stderr always gets it
 *                      too, but stderr is easily lost to a closed terminal.
 */
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#ifdef _WIN32
/* No ucontext and no prctl on Win32. The fault/stall reporters below are
 * rebuilt on the Windows equivalents (a vectored exception handler and a
 * watchdog thread) rather than stubbed out - they are the project's primary
 * diagnostic surface and a silent one would be worse than none. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
/* SDL_MAIN_HANDLED: this file keeps its own main(). See the block at the
 * top of main() for why, and for the SDL_SetMainReady() that goes with it. */
#define SDL_MAIN_HANDLED 1
#include <SDL.h>
#else
#include <ucontext.h>
#include <sys/prctl.h>
#endif
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>
#ifdef _WIN32
#include <direct.h>
/* Thin spellings for the handful of POSIX calls the run-capture path uses.
 * Each is a genuine equivalent, not a stub, except symlink() - Windows needs
 * a privilege for that, and the call site already treats failure as fine
 * ("a plain dir listing still finds it"). */
#define mkdir(p, m)        _mkdir(p)
#define localtime_r(t, tm) (*(tm) = *localtime(t), (tm))
#define symlink(tgt, lnk)  (-1)
#define setenv(k, v, ow)   _putenv_s((k), (v))
#endif
typedef unsigned char u8;

/* Frames completed, from the shim's pump - via an accessor, NOT an extern on
 * the counter itself: sl_frame is static there, and an extern of that name
 * binds to an unrelated function symbol without a murmur from the linker. */
extern unsigned sl_frames_completed(void);

void sl_shim_configure(void);
int  sl_ucode_tables_derive(void);
int  sl_gfx_init(void);
void sl_gfx_begin_frame(void);
void mainproc(void *args);

static void sl_dump_pc_chain(unsigned long ip, unsigned long bp)
{
    int i;
    fprintf(stderr, "  pc 0x%lx\n", ip);
    /* -O0 keeps frame pointers: [ebp]=saved ebp, [ebp+4]=return address */
    for (i = 0; i < 10 && bp > 0x1000 && bp < 0xffffe000ul; i++) {
        unsigned long *f = (unsigned long *) bp;
        fprintf(stderr, "  ra 0x%lx\n", f[1]);
        if (f[0] <= bp) break;
        bp = f[0];
    }
}

int sl_pager_fault(unsigned long addr);
void sl_pager_init(void);

/* Async-signal-safe enough: write(2) only, no allocation, no stdio locks. The
 * report goes to a FILE as well as stderr because a crash in a windowed
 * session usually takes the terminal's scrollback with it. */
static int  g_crash_fd = -1;

/* ---- run capture ---------------------------------------------------------
 *
 * Every launch gets its own directory holding the input stream, the stderr log
 * and the crash report, with no flag to remember.
 *
 * This is the SL_HANG_SECONDS lesson again, three lines down in main(): a
 * facility that must be switched on is never on when it is wanted. The owner
 * asked for exactly this - the playthrough always on disk, the log out of
 * their way but kept for diagnosis. So stderr LEAVES the terminal (one line on
 * stdout says where it went) and the recording is automatic.
 *
 * Replays are not re-recorded: SL_INPUT in means a copy of its own input out,
 * which would bury the human-played runs in the rotation.
 *
 *   SL_RUN=0        disable entirely
 *   SL_RUN_DIR=...  where runs live (default $HOME/.sightline/runs)
 *   SL_RUN_KEEP=n   how many to keep (default 5)
 */
static char g_run_dir[512];
/* The stream this run is RECORDING, whether it named itself or took the run
 * directory's default. Every sidecar is named from this, so that a stream and
 * the things it cannot be replayed without stay together wherever it is
 * written. Empty when the run records nothing (a replay). */
static char g_rec_stream[560];

/* Only ever delete directories this code created - a fixed stamp shape, no
 * separators, no dots. The alternative is a recursive delete steered by
 * whatever happens to be sitting in the directory. */
static int sl_run_name_ok(const char *n)
{
    int i;
    if (n[0] != '2' || n[1] != '0') return 0;
    for (i = 0; n[i]; i++)
        if (!((n[i] >= '0' && n[i] <= '9') || n[i] == '-' ||
              (n[i] >= 'a' && n[i] <= 'z')))
            return 0;
    return i > 8;
}

static void sl_run_rotate(const char *root, int keep)
{
    char names[64][64];
    int n = 0, i, j;
    DIR *d;
    struct dirent *e;

    if (keep < 1) keep = 1;
    if ((d = opendir(root)) == NULL) return;
    while ((e = readdir(d)) != NULL && n < 64) {
        if (!sl_run_name_ok(e->d_name)) continue;
        snprintf(names[n], sizeof names[n], "%s", e->d_name);
        n++;
    }
    closedir(d);

    /* the stamp is fixed-width, so lexicographic order IS chronological */
    for (i = 0; i < n; i++)
        for (j = i + 1; j < n; j++)
            if (strcmp(names[i], names[j]) > 0) {
                char t[64];
                memcpy(t, names[i], sizeof t);
                memcpy(names[i], names[j], sizeof t);
                memcpy(names[j], t, sizeof t);
            }

    for (i = 0; i < n - keep; i++) {
        char dir[600];
        DIR *sub;
        snprintf(dir, sizeof dir, "%s/%s", root, names[i]);
        if ((sub = opendir(dir)) != NULL) {
            while ((e = readdir(sub)) != NULL) {
                char f[700];
                if (e->d_name[0] == '.') continue;
                snprintf(f, sizeof f, "%s/%s", dir, e->d_name);
                unlink(f);
            }
            closedir(sub);
        }
        rmdir(dir);
    }
}

static void sl_run_open(void)
{
    const char *off  = getenv("SL_RUN");
    const char *root = getenv("SL_RUN_DIR");
    const char *keep = getenv("SL_RUN_KEEP");
    const char *lvl  = getenv("SL_BOOT_LEVEL");
    char rootbuf[512], path[600], stamp[64];
    time_t now;
    struct tm tmv;
    FILE *f;

    if (off != NULL && *off == '0') return;

    /* ONLY A PERSON'S RUN IS CAPTURED, and it took two tries to get the test
     * right. The rotation exists to hold what cannot be recreated: a human
     * playthrough. Everything else - replays, bootsweep, probe runs, trace
     * work - is reproducible by re-running the script that produced it.
     *
     * Filtering on "is this a replay" was too narrow. It caught SL_INPUT and
     * missed bootsweep, which drives twenty levels with no SL_INPUT at all, so
     * twenty automated runs six seconds apart evicted the owner's playthrough
     * a second time - after the first fix, from the same rotation, for a
     * different reason.
     *
     * A terminal is the thing automation does not have. Scripts redirect their
     * streams; a person at a prompt does not. So: no tty, no capture. A replay
     * is skipped even from a terminal, since the input stream it would record
     * is a copy of the one it was handed. SL_RUN=1 overrides all of it. */
    if (off == NULL || *off != '1') {
        if (getenv("SL_INPUT") != NULL) return;
        if (!isatty(2) && !isatty(1)) return;
    }

    if (root == NULL) {
#ifdef _WIN32
        const char *home = getenv("USERPROFILE");
        if (home == NULL) home = getenv("HOME");
        if (home == NULL) home = ".";
#else
        const char *home = getenv("HOME");
        if (home == NULL) home = "/tmp";
#endif
        snprintf(path, sizeof path, "%s/.sightline", home);
        mkdir(path, 0755);
        snprintf(rootbuf, sizeof rootbuf, "%s/.sightline/runs", home);
        root = rootbuf;
    }
    if (mkdir(root, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "sightline native: no run dir at %s\n", root);
        return;
    }

    now = time(NULL);
    localtime_r(&now, &tmv);
    snprintf(stamp, sizeof stamp, "%04d%02d%02d-%02d%02d%02d-lvl%s",
             tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
             tmv.tm_hour, tmv.tm_min, tmv.tm_sec, lvl != NULL ? lvl : "boot");
    snprintf(g_run_dir, sizeof g_run_dir, "%s/%s", root, stamp);
    if (mkdir(g_run_dir, 0755) != 0) { g_run_dir[0] = 0; return; }

    sl_run_rotate(root, keep != NULL ? atoi(keep) : 5);

    /* A stable name for "the run that just happened", so a diagnosis never
     * depends on the owner reading a timestamp back to anyone. */
    snprintf(path, sizeof path, "%s/latest", root);
    unlink(path);
    if (symlink(stamp, path) != 0) { /* a plain dir listing still finds it */ }

    /* How it was launched. Every SL_ knob that shaped the run, recorded with
     * the run - otherwise the log and the input stream describe a
     * configuration nobody wrote down. */
    snprintf(path, sizeof path, "%s/env", g_run_dir);
    if ((f = fopen(path, "w")) != NULL) {
        extern char **environ;
        char **ep;
        for (ep = environ; *ep != NULL; ep++)
            if (strncmp(*ep, "SL_", 3) == 0)
                fprintf(f, "%s\n", *ep);
        fclose(f);
    }

    if (getenv("SL_CRASH_LOG") == NULL) {
        snprintf(path, sizeof path, "%s/crash.txt", g_run_dir);
        setenv("SL_CRASH_LOG", path, 1);
    }
    /* THE SIDECARS BELONG TO THE STREAM, NOT TO THE RUN DIRECTORY.
     *
     * This block used to be entered only when SL_INPUT_RECORD was UNSET, so
     * everything below hung off the default `<run>/input` name. Naming the
     * stream yourself - which is the obvious thing to do when the recording is
     * meant to be kept, and exactly what the owner did - silently opted out of
     * every sidecar the stream cannot be replayed without.
     *
     * MEASURED 2026-09-08. The two Dam witnesses were recorded with an
     * explicit SL_INPUT_RECORD=tools/trace/inputs/dam-{fid,pad}.input and an
     * explicit SL_MOVE_RECORD beside it. Their `env` sidecars carry no
     * SL_VIS_RECORD line and no dam-*.vis exists, so the VI/frame
     * correspondence was never written. Replayed, both diverged where the
     * intro camera cuts - not because the input was wrong but because the
     * simulation ran at a cadence the recording never had: 1.22 VI per frame
     * recorded, 1.00 replayed headless, 1.90 replayed windowed.
     *
     * So the stream path is decided FIRST, and the three sidecars are named
     * from it whatever it is. `<stream>.vis`, `<stream>.move` and
     * `<stream>.spec` are the same names the replay side looks for beside
     * SL_INPUT, and for the default `<run>/input` they are byte-for-byte the
     * names this wrote before - `input.vis`, `input.move`, `input.spec`.
     */
    if (getenv("SL_INPUT") == NULL) {
        const char *stream = getenv("SL_INPUT_RECORD");
        if (stream == NULL) {
            snprintf(path, sizeof path, "%s/input", g_run_dir);
            setenv("SL_INPUT_RECORD", path, 1);
            stream = getenv("SL_INPUT_RECORD");
        }
        snprintf(g_rec_stream, sizeof g_rec_stream, "%s", stream);
        /* The input stream alone is NOT enough to replay a live session: it is
         * indexed by VI retraces while the sim advances per frame, so a replay
         * running at a different VI-per-frame ratio applies it out of step and
         * drifts.  Capture the correspondence beside it - see SL_VIS_RECORD in
         * sl_ultra_shim.c.  Runs recorded before 2026-08-26 have no .vis and
         * cannot be replayed faithfully; nothing reconstructs it after the
         * fact for a native session. */
        if (getenv("SL_VIS_RECORD") == NULL) {
            snprintf(path, sizeof path, "%s.vis", g_rec_stream);
            setenv("SL_VIS_RECORD", path, 1);
        }
        /* AND THE MOVEMENT SIDECAR, unconditionally, for the same reason the
         * .vis is written here rather than asked for: the person whose session
         * is worth keeping is not going to set an environment variable first.
         *
         * MEASURED 2026-09-08: without it a session played with keyboard and
         * mouse records a stream in which the stick is (0,0) on every single
         * record, because native keyboard and mouse do not drive the N64 stick
         * - they drive four channels and a look pair that nothing wrote down.
         * The owner's two Dam witnesses were both recorded that way and both
         * replay with Bond standing still. See the SL_MOVE block in
         * sl_ultra_shim.c. The name is `<stream>.move` so that
         * SL_INPUT=<stream> finds it with no second variable to remember. */
        if (getenv("SL_MOVE_RECORD") == NULL) {
            snprintf(path, sizeof path, "%s.move", g_rec_stream);
            setenv("SL_MOVE_RECORD", path, 1);
        }
    }

    /* Say where it went on the TERMINAL, before stderr stops being one. */
    printf("sightline: this run -> %s\n", g_run_dir);
    fflush(stdout);

    /* Take stderr ONLY when it is a terminal. If it is a pipe or a file the
     * caller is already capturing it deliberately, and stealing it produces
     * an empty log with no error - which is exactly what this did to a
     * measurement pipeline on 2026-08-26, silently, for five runs. The point
     * was ever only to keep the owner's terminal quiet; a script's redirect
     * is not a terminal and was never the target. */
    if (isatty(2)) {
        snprintf(path, sizeof path, "%s/log", g_run_dir);
        if (freopen(path, "w", stderr) != NULL)
            setvbuf(stderr, NULL, _IOLBF, 0);  /* a crash must not eat the tail */
    } else {
        fprintf(stderr, "sightline native: run dir %s (stderr left alone - "
                        "not a terminal)\n", g_run_dir);
    }
}

/* The RNG state this run will actually use, recorded the moment the game has
 * committed to it.
 *
 * WHY THE RUN NEEDS IT AT ALL. boss.c seeds the game RNG at boss start and
 * every draw from the level load onward - intro camera, spawn look angle, AI
 * reaction timers - hangs off that one state. A recording that carries only
 * the input stream therefore does not describe its own run: replay it against
 * a different boot seed and the same buttons produce a different game.
 *
 * It goes in the `env` sidecar rather than a new file because that sidecar
 * already IS this run's metadata - "every SL_ knob that shaped the run" - and
 * SL_RNG_SEED is exactly such a knob, just one the game picks rather than the
 * launcher. Written as an assignment so the file stays literally what it
 * claims to be: the environment that reproduces this run.
 *
 * APPENDED, not written by sl_run_open(), because the seed does not exist yet
 * at launch. sl_run_open() runs from main() before mainproc(); boss.c chooses
 * the seed well after that.
 *
 * A run with no run directory - a replay, or anything without a terminal, see
 * sl_run_open() - has nowhere to append, and that is correct: such a run is
 * reproducible from the script that started it. The state is printed to stderr
 * in every case regardless, so it is never unrecoverable.
 *
 * COMPATIBILITY: a run directory recorded before this existed has no
 * SL_RNG_SEED line. Nothing reconstructs it after the fact - the state is not
 * derivable from the frame count or the input stream - so such a recording
 * replays on the default boot seed, which is what it was recorded on. See
 * sl_rng_seed_override() in sl_ultra_shim.c, which is where that promise is
 * kept.
 */
void sl_run_note_rng_seed(unsigned long long state)
{
    char path[600];
    const char *lvl, *diff;
    FILE *f;

    if (g_run_dir[0] == 0) return;
    snprintf(path, sizeof path, "%s/env", g_run_dir);
    if ((f = fopen(path, "a")) != NULL) {
        fprintf(f, "SL_RNG_SEED=%016llx\n", state);
        fclose(f);
    }

    /* AND BESIDE THE STREAM ITSELF, in the .spec convention this repository
     * already uses for recordings (tools/trace/inputs/*.spec).
     *
     * A stream is NOT replayable without the RNG state it was recorded under:
     * boss.c seeds the game RNG at boss start and every draw from the level
     * load onward hangs off it - the intro camera pick, the spawn look angle,
     * AI reaction timers. Until now that state was written ONLY to the run's
     * `env` file, so a stream copied out of a run directory and committed
     * arrived without it and replayed under the deterministic boot default
     * instead. That artefact LOOKS reproducible and is not, which is the
     * expensive shape: it replays cleanly, it just replays a different run.
     *
     * MEASURED 2026-09-08: the three Dam recordings each carried a DIFFERENT
     * state - 000000007a91b2e1, 00000000527d4edd, 0000000000000004 - visible
     * nowhere but their own run directories. Writing it here means the stream
     * and the one number it cannot be replayed without travel together by
     * default, with nothing for anyone to remember.
     *
     * SEED= is an ADDITION to the existing key=value line, alongside LEVEL and
     * DIFFICULTY so the sidecar says what it is. Nothing reads these files
     * positionally, so every existing .spec stays valid and this one is a
     * superset of them. */
    /* BESIDE THE STREAM, not beside the run directory. Same defect as the .vis
     * and for the same reason: a stream that names itself
     * (SL_INPUT_RECORD=tools/trace/inputs/dam-fid.input) used to leave its
     * SEED= behind in a run directory nobody copies, so the two Dam witnesses
     * arrived in the tree with no .spec at all and replayed on the boot
     * default. For the run directory's own default stream `<run>/input` this
     * writes `<run>/input.spec` - the identical path it always did.
     *
     * A TRAILING `.input` IS STRIPPED, and that is not cosmetic. This
     * repository's .spec convention is `<base>.spec` beside `<base>.input`,
     * and tools/trace/rebuild_roms.py globs `*.spec` and takes the recording's
     * NAME from the file's stem (:81,:90). Writing `dam-pad.input.spec` would
     * hand it the name "dam-pad.input" and it would try to build a direct-boot
     * ROM for a level of that name. sl_rng_seed_override reads both shapes -
     * it tries `<stream>.spec` first and then strips `.input` - so the stripped
     * form is found on replay either way, and `<run>/input` does not end in
     * `.input` and so keeps the exact path it always had. */
    if (g_rec_stream[0] == 0) return;    /* records nothing: nothing to stamp */
    {
        size_t sn = strlen(g_rec_stream);
        if (sn > 6 && strcmp(g_rec_stream + sn - 6, ".input") == 0)
            snprintf(path, sizeof path, "%.*s.spec", (int) (sn - 6), g_rec_stream);
        else
            snprintf(path, sizeof path, "%s.spec", g_rec_stream);
    }
    if ((f = fopen(path, "w")) == NULL) return;
    lvl  = getenv("SL_BOOT_LEVEL");
    diff = getenv("SL_BOOT_DIFFICULTY");
    fprintf(f, "LEVEL=%s DIFFICULTY=%s SEED=%016llx\n",
            lvl != NULL ? lvl : "boot", diff != NULL ? diff : "", state);
    fclose(f);
}

/* THE EVENT LOG: what the GAME did, not what the renderer drew.
 *
 * The run log was ~13000 lines of renderer telemetry - textures, render modes,
 * matrices, triangles - and not one game event. You could not tell from a
 * recording whether the player reached the gas, died, or changed level, which
 * made every question about a playthrough depend on replaying it with probes
 * compiled in. The owner asked for the obvious thing: see when fog gets
 * triggered in the actual session.
 *
 * This WATCHES rather than instruments. It reads a few game globals at the
 * frame boundary and writes a line when one changes, so it needs no call sites
 * in src/game - nothing to keep in sync, nothing to guard with #ifdef __sgi,
 * and no risk to the matching build.
 *
 * g_ScaledFarFogIntensity is the gas signal: facility runs at ~5000 and the
 * tank explosion lerps it to ~1000, which is also what pulls the far clip
 * plane in. A transition in it IS the gas starting.
 *
 * Cost is one float compare and one int compare per frame. Events are stamped
 * with the record index, the same coordinate marks use, so an event and a mark
 * can be read against each other directly. */
void sl_watch_frame(unsigned record_index)
{
    extern float g_ScaledFarFogIntensity;
    extern int   g_GlobalTimer;
    static FILE *ev;
    static int   tried;
    static float prev_fog = -1.0f;
    static int   prev_timer = -1;
    char path[600];

    if (g_run_dir[0] == 0) return;
    if (!tried) {
        tried = 1;
        snprintf(path, sizeof path, "%s/events", g_run_dir);
        ev = fopen(path, "w");
        if (ev != NULL)
            fprintf(ev, "# record  event\n");
    }
    if (ev == NULL) return;

    /* the level restarting shows up as the timer going backwards */
    if (g_GlobalTimer < prev_timer)
        fprintf(ev, "%8u  level-restart (timer %d -> %d)\n",
                record_index, prev_timer, g_GlobalTimer);
    prev_timer = g_GlobalTimer;

    /* The environment COLOUR is what the ROM was measured lerping: Facility
     * runs at (16,32,16) and the gas walks it toward the alt row's (64,128,64)
     * over thousands of frames. bgfog.c feeds these three bytes straight to
     * gDPSetFogColor, so this is the fog colour itself. Logging it makes our
     * run directly comparable with romprobe's dump of the same symbol. */
    {
        extern u8 g_CurrentEnvironment[];
        static int pr = -1, pg = -1, pb = -1;
        /* Offsets 8,9,10. The ROM dump reads 00 10 20 10 from env+8, i.e. a
         * pad byte then R,G,B - reading 9,10,11 logged G,B,pad and printed
         * "32,16,0" for what is really (16,32,16). */
        int r = g_CurrentEnvironment[8];
        int g = g_CurrentEnvironment[9];
        int b = g_CurrentEnvironment[10];
        if (r != pr || g != pg || b != pb) {
            fprintf(ev, "%8u  env-rgb %3d,%3d,%3d\n", record_index, r, g, b);
            pr = r; pg = g; pb = b;
            fflush(ev);
        }
    }

    /* The far clip plane the world is actually drawn with. The cartridge runs
     * 5000 and lerps toward 1000 during the gas (measured live with
     * tools/native/romprobe.py); our NDC depths were pinned at exactly 1.00,
     * which is what a far plane of ~300 looks like with a room deeper than
     * that. If these disagree we clip away most of the room and show backdrop
     * where the ROM shows fogged geometry. */
    {
        extern void *g_ViBackData;
        static float prev_zf = -1.0f;
        /* VideoSettings_s: 4 x 1-byte, then s16 x,y, then f32 fovy,
         * aspect, znear, zfar - so zfar sits at byte 20. */
        float zf = g_ViBackData
                 ? *(float *)((char *)g_ViBackData + 20) : -1.0f;
        if (zf != prev_zf) {
            fprintf(ev, "%8u  zfar %.1f -> %.1f\n", record_index, prev_zf, zf);
            prev_zf = zf;
            fflush(ev);
        }
    }

    if (g_ScaledFarFogIntensity != prev_fog) {
        fprintf(ev, "%8u  far-fog %.1f -> %.1f%s\n", record_index,
                prev_fog, g_ScaledFarFogIntensity,
                (prev_fog > 2000.0f && g_ScaledFarFogIntensity < 2000.0f)
                    ? "   <-- GAS" : "");
        prev_fog = g_ScaledFarFogIntensity;
        fflush(ev);
    }
}

/* A MARK is the owner pointing at something.
 *
 * Diagnosis kept stalling on "which moment?" - two five-minute recordings
 * reached neither the tanks nor a room anyone could identify, and the science
 * lab's room index was still unknown after three rounds of measurement. The
 * person playing knows exactly when they see it; they just had no way to say
 * so. F9 writes the read index they saw it at, next to the recording, and the
 * replay can go straight there.
 *
 * The read index is the right coordinate because it is what replay is indexed
 * by - not wall time, not a frame counter that direct boot and replay disagree
 * about. Marks only exist for captured runs, which are the human ones. */
/* SIGUSR1 marks as well as F9 does.
 *
 * Two reasons, and the second is why it exists at all. It lets a replay be
 * marked from outside with `kill -USR1`, which F9 cannot do. And it makes the
 * mark path testable with no X server, no window manager and no focus - the
 * first attempt to verify F9 under xvfb failed with BadWindow because there is
 * no window manager to give the window keyboard focus, which proved nothing
 * about the feature and everything about the harness.
 *
 * A handler may not call fopen or fprintf, so it only raises a flag; the pad
 * read path takes it on the next controller read and does the writing. */
static volatile sig_atomic_t g_mark_pending;

static void sl_on_mark_signal(int sig)
{
    (void) sig;
    g_mark_pending = 1;
}

int sl_mark_pending_take(void)
{
    if (!g_mark_pending) return 0;
    g_mark_pending = 0;
    return 1;
}

void sl_run_mark(unsigned read_index)
{
    static unsigned n;
    char path[600];
    FILE *f;

    if (g_run_dir[0] == 0) return;        /* not a captured run */
    snprintf(path, sizeof path, "%s/marks", g_run_dir);
    if ((f = fopen(path, "a")) == NULL) return;
    n++;
    fprintf(f, "mark %u read %u\n", n, read_index);
    fclose(f);
    printf("sightline: mark %u recorded at read %u\n", n, read_index);
    fflush(stdout);
}

/* ---- LIVE OWNER BUG MARK (F8) -------------------------------------------
 *
 * F9 already records "I saw it at read index N" - one integer, next to the
 * recording, so a replay can jump there. That was built on the assumption
 * that the replay would then reproduce what the owner saw. It does not: the
 * measured divergence between a native recording and its replay reaches 202
 * world units by sample 4201 and 26464 at worst, so a mark that only names a
 * MOMENT points into a run that never happened.
 *
 * F8 therefore captures the moment itself, in the run that is happening, and
 * needs no replay to be readable:
 *
 *   <run>/mark-NNN.txt   camera, rooms, projection, and the draws that
 *                        projected onto the centre of the screen
 *   <run>/mark-NNN.bmp   the framebuffer of THAT SAME FRAME
 *
 * The frame correspondence is structural rather than hoped for: the renderer
 * arms its capture at the top of one display list (sl_gfx_frame_dl) and this
 * is called from the backend's end_frame BEFORE SDL_GL_SwapWindow, so the
 * back buffer being read is the one the walk just filled.
 *
 * BMP rather than PPM. The existing SL_SHOT path writes PPM, which nothing on
 * Windows opens by double-clicking; a capture the owner cannot look at is a
 * capture that does not get looked at. BMP is bottom-up 24-bit BGR, which is
 * exactly what glReadPixels hands back with the rows already in the right
 * order - so it costs one channel swap and no dependency.
 *
 * NUMBERED, NOT TIMESTAMPED, and the number is allocated here so the .txt and
 * the .bmp cannot drift apart. The F9 `marks` file keeps its own numbering and
 * is untouched.
 */
static unsigned g_mark_full_n;

const char *sl_run_dir(void);
const char *sl_run_dir(void)
{
    return g_run_dir[0] != 0 ? g_run_dir : NULL;
}

/* Called from the graphics backend at the frame boundary. Does nothing at all
 * unless a capture is waiting, so both call sites can invoke it blind. */
void sl_run_mark_full(int (*shoot)(const char *path),
                      int (*probe)(char *out, int n));
void sl_run_mark_full(int (*shoot)(const char *path),
                      int (*probe)(char *out, int n))
{
    extern int  sl_mark_ready(void);
    extern void sl_mark_consume(void);
    extern int  sl_mark_render(char *out, int n,
                               int (*resolve)(unsigned addr, int *kind));
    extern int  sl_mark_game_render(char *out, int n);
    extern int  sl_mark_room_of_dl(unsigned addr, int *kindout);
    extern int  sl_mark_subs_render(char *out, int n,
                                    int (*resolve)(unsigned addr, int *kind));
    static char buf[640 * 1024];
    char txt[600], img[600];
    unsigned num;
    int at = 0, shot = 0, k;
    FILE *f;

    if (!sl_mark_ready()) return;
    sl_mark_consume();

    if (g_run_dir[0] == 0) {
        /* No run directory means an automated run (see sl_run_open) - there is
         * nowhere this belongs. Say so rather than write nothing: a mark that
         * silently does not appear is indistinguishable from a key that is not
         * bound, and this repository has six recorded silent instruments. */
        fprintf(stderr, "sl_mark: F8 pressed but this run has no run directory"
                        " (not a terminal session, or SL_RUN=0) - nothing"
                        " written\n");
        return;
    }

    num = ++g_mark_full_n;
    snprintf(txt, sizeof txt, "%s/mark-%03u.txt", g_run_dir, num);
    snprintf(img, sizeof img, "%s/mark-%03u.bmp", g_run_dir, num);

    if (shoot != NULL)
        shot = shoot(img);

    {
        extern unsigned sl_frames_completed(void);
        extern unsigned sl_record_index(void);
        k = snprintf(buf, sizeof buf,
            "sightline bug mark %u\n"
            "=====================\n"
            "\n[mark]\n"
            "mark-number         %u\n"
            "run-dir             %s\n"
            "screenshot          %s\n"
            "pumped-frame        %u   (sl_frames_completed)\n"
            "vi-retrace-index    %u   (sl_record_index - the unit F9 marks and\n"
            "                          the unit an input recording is indexed by)\n"
            "sections            [mark] [camera] [projection] [rooms]\n"
            "                    [submitted-lists] [provenance-integrity]\n"
            "                    [centre-pixel] [renderer] [candidate-draws]\n"
            "\n",
            num, num, g_run_dir,
            shot ? img : "NONE (no window backend, or the read-back failed)",
            sl_frames_completed(), sl_record_index());
        at = (k > 0) ? k : 0;
    }

    /* [camera] [projection] [rooms] - the simulation's half. */
    at += sl_mark_game_render(buf + at, (int) sizeof buf - at);

    /* ...and the SUBMISSION PROVENANCE only the RENDERER can supply, resolved
     * against the game's rooms here.
     *
     * THE JOIN. The renderer names lists and does not know what a room is; the
     * game names rooms and cannot see which list was submitted. Resolving here
     * keeps both halves honest about what they know, and an unresolved list is
     * printed as unresolved rather than guessed at.
     *
     * The table used to be assembled in this function out of six scalar
     * accessors, and it described only the submissions the FRAME LIST made -
     * so it stopped at the first list that carried the rest of the frame
     * inside it, which in the owner's Dam marks is the trailing heap list
     * holding the props, the characters and the viewmodel. The renderer now
     * prints the whole tree itself, where the ucode05 call/branch semantics
     * live, and takes this resolver as a callback. */
    if (at < (int) sizeof buf - 4096) {
        if (at < (int) sizeof buf - 2) { buf[at++] = '\n'; buf[at] = 0; }
        at += sl_mark_subs_render(buf + at, (int) sizeof buf - at,
                                  sl_mark_room_of_dl);
    }

    /* [centre-pixel] - what the marked framebuffer actually holds there. */
    if (at < (int) sizeof buf - 4096) {
        int got = 0;
        k = snprintf(buf + at, sizeof buf - at, "\n");
        if (k > 0) at += k;
        if (probe != NULL)
            got = probe(buf + at, (int) sizeof buf - at);
        if (got > 0) {
            at += got;
        } else {
            k = snprintf(buf + at, sizeof buf - at,
                "[centre-pixel]\n"
                "UNAVAILABLE - no window backend on this run, or the framebuffer\n"
                "read-back failed. No colour and no depth are reported rather than\n"
                "a plausible-looking zero.\n");
            if (k > 0) at += k;
        }
    }

    /* [renderer] [candidate-draws] - the interpreter's half, with the room
     * resolver handed in so each candidate can be classified where it is
     * printed. */
    if (at < (int) sizeof buf - 2) { buf[at++] = '\n'; buf[at] = 0; }
    at += sl_mark_render(buf + at, (int) sizeof buf - at, sl_mark_room_of_dl);

    if ((f = fopen(txt, "w")) == NULL) {
        fprintf(stderr, "sl_mark: cannot write %s\n", txt);
        return;
    }
    fwrite(buf, 1, (size_t) at, f);
    fclose(f);

    /* On the TERMINAL, not just in the log: stderr has been redirected into
     * the run directory by now, so a message there is invisible to somebody
     * playing. The owner needs to see that the key did something. */
    printf("sightline: bug mark %u -> %s%s\n", num, txt,
           shot ? " (+ .bmp)" : " (no screenshot)");
    fflush(stdout);
}

static void sl_crash_open(void)
{
    const char *p = getenv("SL_CRASH_LOG");
    char buf[64];

    if (p == NULL) {
        /* no snprintf in a handler; build it once, here, before any fault */
        char *q = buf;
        const char *pre = "/tmp/sightline-crash-";
        int pid = (int) getpid();
        char num[16];
        int n = 0;
        while (*pre) *q++ = *pre++;
        if (pid == 0) num[n++] = '0';
        while (pid > 0) { num[n++] = (char) ('0' + pid % 10); pid /= 10; }
        while (n > 0) *q++ = num[--n];
        *q++ = '.'; *q++ = 't'; *q++ = 'x'; *q++ = 't'; *q = 0;
        p = buf;
    }
    g_crash_fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    fprintf(stderr, "sightline native: crash reports -> %s\n", p);
}

static void sl_say(const char *s)
{
    unsigned long n = 0;
    while (s[n]) n++;
    if (write(2, s, n) < 0) { /* stderr may be gone; the file still matters */ }
    if (g_crash_fd >= 0 && write(g_crash_fd, s, n) < 0) { }
}

#ifdef SL_ALHEAP_TRACE
/* EVERYTHING below is measurement scaffolding - the reporters AND their
 * storage. It was previously unguarded: all ten symbols, including four
 * 512-entry arrays, shipped in the production skeleton as dead code. Nothing
 * called them so behaviour was unaffected and the gates passed, which is
 * exactly why reading the source was not enough - `nm` on the built artefact
 * was. A guard that covers the callers but not the definitions is not a guard.
 */
/* Audio-heap demand records, filled by src/libultra/audio/heapalloc.c under
 * -DSL_ALHEAP_TRACE. Defined here because the decomp side cannot include
 * <stdio.h> (the repo shadows it), and printed from the crash handler so a
 * fault still yields the demand table that explains it. */
long sl_alheap_n = 0;
long sl_alheap_req[512], sl_alheap_used[512], sl_alheap_len[512], sl_alheap_line[512];
unsigned char sl_alheap_ok[512];
const char *sl_alheap_file[512];
void sl_audio_initreport(int seqCount, void *t1, void *t2, void *t3)
{
    if (seqCount >= 0)
        fprintf(stderr, "AUDIOINIT seqCount=%d\n", seqCount);
    else
        fprintf(stderr, "AUDIOINIT seqPlayers t1=%p t2=%p t3=%p (non-null: %d of 3)\n",
                t1, t2, t3, (t1 != 0) + (t2 != 0) + (t3 != 0));
}
void sl_seq_proof(const void *raw, unsigned entsz, unsigned rawsize, unsigned aligned)
{
    const unsigned char *b = (const unsigned char *) raw;
    unsigned nat, be; int i;
    nat = *(const unsigned short *) raw;              /* current native read */
    be  = (unsigned)((b[0] << 8) | b[1]);             /* documented big-endian */
    fprintf(stderr, "SEQPROOF raw16:");
    for (i = 0; i < 16; i++) fprintf(stderr, " %02x", b[i]);
    fprintf(stderr, "\n  first two bytes %02x %02x\n", b[0], b[1]);
    fprintf(stderr, "  native direct read = %u\n", nat);
    fprintf(stderr, "  big-endian decode  = %u\n", be);
    fprintf(stderr, "  entry size %u ; raw table size %u ; aligned request %u\n",
            entsz, rawsize, aligned);
    fprintf(stderr, "  bad-if-raw would be %u (aligned %u); correct %u (aligned %u)\n",
            entsz * nat + 4, ((entsz * nat + 4) + 15) & ~15u,
            entsz * be  + 4, ((entsz * be  + 4) + 15) & ~15u);
}
/* Native ACMD stream capture. Writes a flat task-indexed stream so the native
 * and cartridge populations can be counted BEFORE any word is compared. */
/* Characterise the earliest playback failure: what the table says, what was
 * fetched, and whether the decompressed bytes look like sequence data at all,
 * tested against the documented container (a 32-bit offset to the music data,
 * documented as usually 0x44) rather than against plausibility. */
/* SEAM IDENTITY. Anchored on event identity - which track, which sequence entry,
 * and that entry's two lengths - not on frame number, because frames can drift
 * for reasons unrelated to the sequence data while track and entry cannot. Both
 * sides must report the same tuple before any content byte is compared. */
/* Native side of the buffer comparison. Same sanity checks as the cartridge
 * capture: all-zero, single-valued or short is an instrument failure and must
 * say so rather than be compared. */
/* The parser's own view of the header - the earliest differing state. division
 * is documented as 0x180; trackOffset[0] as the header size 0x44. */
void sl_poll_balance_report(void)
{
    extern unsigned long sl_retrace_n, sl_joypoll_n, sl_viadv_n;
    /* joyPoll is NOT the input-stream consumer - the controller handshake
     * stand-in also calls it, legitimately, without advancing the stream.
     * sl_input_vi_advance IS the consumer, and that is what must be exactly
     * once per retrace. Both are reported so the difference stays visible. */
    { extern unsigned long sl_pace_calls, sl_pace_skips, sl_pace_resync, sl_pace_slept;
      extern double sl_pace_slept_s; extern unsigned long sl_fadv_calls;
      { extern int currentFrameCounter; extern unsigned int g_AudioFrameCount;
        fprintf(stderr, "CLOCKS retraces=%lu currentFrameCounter=%d "
                        "frameBoundaries=%lu audioTasks=%u\n",
                sl_retrace_n, currentFrameCounter, sl_fadv_calls, g_AudioFrameCount); }
      fprintf(stderr, "PACE calls=%lu skipped=%lu resync=%lu slept=%lu total=%.2fs"
                      "  frameAdvance(sim ticks)=%lu\n",
              sl_pace_calls, sl_pace_skips, sl_pace_resync, sl_pace_slept,
              sl_pace_slept_s, sl_fadv_calls); }
    fprintf(stderr, "POLLBALANCE retraces=%lu inputAdvance=%lu  %s\n",
            sl_retrace_n, sl_viadv_n,
            sl_retrace_n == sl_viadv_n ? "INPUT ADVANCED EXACTLY ONCE PER RETRACE"
                                       : "*** IMBALANCED ***");
    fprintf(stderr, "  joyPoll calls=%lu (excess %ld is the handshake stand-in, "
                    "which does not advance the stream)\n",
            sl_joypoll_n, (long) sl_joypoll_n - (long) sl_retrace_n);
}

/* AUDIO LIFECYCLE REPORT. Reads counters kept by audi.c (the decomp side has no
 * host stdio) and prints them here. Every assertion below is stated together
 * with the population it was taken over, and a control over an EMPTY population
 * is reported as VACUOUS, never as a pass - the first version of this report
 * called three controls "passed" while sl_audio_step had run zero times. */
/* Envelope sanity, first few only. ALMicroTime is microseconds. */
void sl_env_probe(int at, int dt, int rt, int av, int dv)
{
    static int n = 0, bad = 0;
    if (rt < 0 || rt > 60000000 || at < 0 || at > 60000000) bad++;
    if (n++ < 6)
        fprintf(stderr, "ENV[%d] attack=%d decay=%d release=%d us  "
                        "attackVol=%d decayVol=%d\n", n - 1, at, dt, rt, av, dv);
    if (n == 200)
        fprintf(stderr, "ENVPROBE %d envelopes, %d with an implausible time "
                        "(>60s or negative)\n", n, bad);
}
/* NOTE-OFF PARTITION report. Decides between not-generated, generated-and-
 * dropped, and generated-late, rather than leaving "notes sustain too long"
 * as an undifferentiated symptom. */
/* The sequence's own track table. A track is live iff its offset is non-zero
 * (cseq.c:64-68), so this says which tracks the player will actually walk and
 * where each one starts - the two facts a per-track divergence needs. */
void sl_seq_tracks(const unsigned *off, unsigned valid)
{
    static int n = 0; int i;
    if (n++ > 0) return;
    fprintf(stderr, "SEQTRACKS validTracks=%#06x  offsets:", valid);
    for (i = 0; i < 16; i++) fprintf(stderr, " %u:%#x", i, off[i]);
    fprintf(stderr, "\n");
}
void sl_sfx_report(void)
{
    extern unsigned long sl_sfx_handler_n, sl_sfx_clients, sl_sfx_registered,
                         sl_sfx_init_ran;
    extern void sl_sfx_probe(void);
    sl_sfx_probe();
    fprintf(stderr, "SFXPLAYER initRan=%lu onDriverList=%lu of %lu client(s) "
                    "handlerCalls=%lu\n",
            sl_sfx_init_ran, sl_sfx_registered, sl_sfx_clients, sl_sfx_handler_n);
    if (!sl_sfx_init_ran)
        fprintf(stderr, "  INERT: sndNewPlayerInit never reached alSynAddPlayer.\n");
    else if (sl_sfx_registered != 1)
        fprintf(stderr, "  INERT: the effects player is not on the driver's "
                        "client list exactly once.\n");
    else if (sl_sfx_handler_n == 0)
        fprintf(stderr, "  INERT: registered but its handler never ran.\n");
    else
        fprintf(stderr, "  LIVE: registered once and ticked %lu time(s).\n",
                sl_sfx_handler_n);
    { extern unsigned long sl_sfx_play_evt, sl_sfx_voice_ok, sl_sfx_voice_fail;
      /* ticked is not the same as producing sound: a live player with nothing
       * asked of it is a different gap from an inert one, so count the PLAY
       * events the game raises and the voices they actually take. */
      fprintf(stderr, "  playEvents=%lu voicesAllocated=%lu allocFailed=%lu\n",
              sl_sfx_play_evt, sl_sfx_voice_ok, sl_sfx_voice_fail);
      { extern unsigned long sl_sfx_call, sl_sfx_bootsw, sl_sfx_idx0, sl_sfx_nobank;
        fprintf(stderr, "  sndPlaySfx calls=%lu  refused: bootswitch=%lu "
                        "soundIndex0=%lu nullBank=%lu\n",
                sl_sfx_call, sl_sfx_bootsw, sl_sfx_idx0, sl_sfx_nobank);
        if (sl_sfx_call == 0)
            fprintf(stderr, "  sndPlaySfx IS NEVER CALLED - the gap is upstream "
                            "of the audio system, in whatever requests effects.\n");
        else if (sl_sfx_play_evt == 0)
            fprintf(stderr, "  called but no PLAY event was ever posted - the "
                            "refusal is inside sndPlaySfx.\n"); } }
}
void sl_pvoice_report(void)
{
    extern unsigned long sl_pv_lame, sl_pv_free, sl_pv_steal, sl_pv_stealfail;
    fprintf(stderr, "PVOICE fromLameList=%lu fromFreeList=%lu stealAttempts=%lu\n",
            sl_pv_lame, sl_pv_free, sl_pv_steal);
    if (sl_pv_lame + sl_pv_free + sl_pv_steal == 0)
        fprintf(stderr, "  VACUOUS: no physical voice was allocated.\n");
    else if (sl_pv_steal == 0)
        fprintf(stderr, "  the pool was NEVER exhausted - stealing never ran\n");
}
/* NOTE-ON ORACLE. One fixed record per parsed note-on, in order, written to
 * SL_NOTEON_OUT. Not a ring: the whole ordered population is the oracle, and a
 * ring that wrapped would silently discard the tail. Cursors are recorded as
 * OFFSETS from the first cursor seen, never as raw pointers - absolute
 * addresses differ between builds and arms and must never be compared. */
void sl_noteon_record(unsigned trk, unsigned status, unsigned key, unsigned vel,
                      unsigned long curBefore, unsigned long curAfter,
                      unsigned bytes, unsigned long dur, unsigned fromBackup)
{
    static FILE *f = 0; static int opened = 0; static unsigned long base = 0;
    static unsigned long ord = 0;
    if (!opened) {
        const char *p = getenv("SL_NOTEON_OUT");
        opened = 1;
        if (p) { f = fopen(p, "w"); }
    }
    if (!f) return;
    if (base == 0) base = curBefore;
    fprintf(f, "%lu trk=%u ch=%u key=%u vel=%u curB=+%ld curA=+%ld bytes=%u "
               "dur=%lu backupBytes=%u\n",
            ord++, trk, status & 0x0F, key, vel,
            (long) (curBefore - base), (long) (curAfter - base),
            bytes, dur, fromBackup);
    fflush(f);
}
void sl_duration_report(void)
{
    extern unsigned long sl_dur_hist[10], sl_dur_by_track[16], sl_dur_big_by_track[16];
    extern unsigned long sl_dur_bad_bu, sl_dur_blockcode, sl_dur_esc, sl_vl_frombu;
    int i; unsigned long tot = 0, big = 0;
    for (i = 0; i < 16; i++) { tot += sl_dur_by_track[i]; big += sl_dur_big_by_track[i]; }
    fprintf(stderr, "DURATION noteOnDurations=%lu  over1536ticks=%lu\n", tot, big);
    if (tot == 0) { fprintf(stderr, "  VACUOUS: no duration was read.\n"); return; }
    fprintf(stderr, "  per track (total/over1536):");
    for (i = 0; i < 16; i++)
        if (sl_dur_by_track[i])
            fprintf(stderr, " t%d=%lu/%lu", i, sl_dur_by_track[i], sl_dur_big_by_track[i]);
    fprintf(stderr, "\n  bytes per varlen:");
    for (i = 1; i < 10; i++)
        if (sl_dur_hist[i]) fprintf(stderr, " %d->%lu", i, sl_dur_hist[i]);
    fprintf(stderr, "\n  back-reference path taken %lu time(s), of which escaped "
                    "literals %lu; total bytes served from the backup buffer %lu\n",
            sl_dur_blockcode, sl_dur_esc, sl_vl_frombu);
    fprintf(stderr, "  large durations whose varlen used a backup byte: %lu of %lu\n",
            sl_dur_bad_bu, big);
    { extern unsigned long sl_dur_val[64], sl_dur_trk[64], sl_dur_n;
      unsigned long j;
      /* the values themselves - a threshold I chose is not evidence, the
       * distribution is. 384 ticks is a quarter note (the measured division). */
      fprintf(stderr, "  durations (track:ticks:quarterNotes):");
      for (j = 0; j < sl_dur_n; j++)
          fprintf(stderr, " %lu:%lu:%.2f", sl_dur_trk[j], sl_dur_val[j],
                  sl_dur_val[j] / 384.0);
      fprintf(stderr, "\n"); }
}
void sl_noteoff_report(void)
{
    extern unsigned long sl_np_on, sl_np_off_sched, sl_np_zero_dur;
    extern unsigned long sl_np_off_proc, sl_np_off_nullvoice;
    extern unsigned long sl_np_uspt, sl_np_dur_min, sl_np_dur_max;
    extern unsigned long sl_np_dt_min, sl_np_dt_max;

    { extern unsigned long sl_mus_play_af, sl_mus_play_n, sl_np_first_af, sl_np_first_set;
      fprintf(stderr, "MUSICSTART musicTrack1Play calls=%lu firstAtAudioFrame=%lu"
                      "   firstNoteOnAtAudioFrame=%lu\n",
              sl_mus_play_n, sl_mus_play_af,
              sl_np_first_set ? sl_np_first_af : 0);
      fprintf(stderr, "  cartridge's first VOICE is at audio frame 110 "
                      "(measured); both counters start at amCreateAudioManager\n"); }
    fprintf(stderr, "NOTEOFF noteOn=%lu scheduled=%lu zeroDuration=%lu "
                    "processed=%lu noVoiceToStop=%lu\n",
            sl_np_on, sl_np_off_sched, sl_np_zero_dur,
            sl_np_off_proc, sl_np_off_nullvoice);
    if (sl_np_on == 0) {
        fprintf(stderr, "  VACUOUS: no note-on was processed - nothing is "
                        "asserted about note-offs.\n");
        return;
    }
    if (sl_np_off_sched == 0)
        fprintf(stderr, "  NOT GENERATED: every note-on carried duration 0.\n");
    else
        fprintf(stderr, "  uspt=%lu us/tick  duration ticks %lu..%lu  "
                        "scheduled delay %lu..%lu us (%lu..%lu ms)\n",
                sl_np_uspt, sl_np_dur_min, sl_np_dur_max,
                sl_np_dt_min, sl_np_dt_max,
                sl_np_dt_min / 1000, sl_np_dt_max / 1000);
    if (sl_np_off_sched > sl_np_off_proc)
        fprintf(stderr, "  %lu scheduled note-off(s) never processed\n",
                sl_np_off_sched - sl_np_off_proc);
}
void sl_audio_lifecycle_report(void)
{
    extern unsigned long sl_am_steps, sl_am_frames, sl_am_completions, sl_am_slotadv;
    extern unsigned long sl_am_qual_n, sl_am_qual_enq, sl_am_nonqual_n, sl_am_nonqual_enq;
    extern unsigned long sl_am_notify_bad, sl_am_lastinfo_adv, sl_am_missing_completion;
    extern unsigned long sl_am_reg_hits, sl_am_reg_clients, sl_am_reg_nextword;
    extern unsigned long sl_am_ctl_fired;
    extern unsigned long sl_am_slot_seq[32], sl_am_slot_seq_n;
    extern unsigned long sl_am_evt_seq[32];
    extern long sl_am_lastinfo_seq[32];
    extern unsigned int  sl_am_first_slot_bad;
    extern void sl_audio_registration_probe(void);
    unsigned long i;

    sl_audio_registration_probe();
    fprintf(stderr, "AUDIOREG clientsOnSchedulerList=%lu audioClientOccurrences=%lu "
                    "notifyWord=%lu\n",
            sl_am_reg_clients, sl_am_reg_hits, sl_am_reg_nextword);
    if (sl_am_reg_hits != 1) {
        fprintf(stderr, "  FAIL: the audio client is not on the scheduler's list "
                        "exactly once - everything below is vacuous.\n");
        return;
    }
    fprintf(stderr, "AUDIONOTIFY qualifying=%lu ofWhichEnqueued=%lu | "
                    "nonQualifying=%lu ofWhichSilent=%lu | violations=%lu\n",
            sl_am_qual_n, sl_am_qual_enq, sl_am_nonqual_n, sl_am_nonqual_enq,
            sl_am_notify_bad);
    if (sl_am_qual_n == 0 || sl_am_nonqual_n == 0)
        fprintf(stderr, "  VACUOUS: one of the two retrace classes never "
                        "occurred - the condition was not exercised.\n");

    fprintf(stderr, "AUDIOLIFE steps=%lu frames=%lu completions=%lu "
                    "slotAdvancedByHandler=%lu lastInfoAdvances=%lu "
                    "missingCompletions=%lu ctlFired=%lu\n",
            sl_am_steps, sl_am_frames, sl_am_completions, sl_am_slotadv,
            sl_am_lastinfo_adv, sl_am_missing_completion, sl_am_ctl_fired);
    if (sl_am_steps == 0) {
        fprintf(stderr, "  VACUOUS: sl_audio_step never ran - no audio client "
                        "message was ever queued. Nothing is asserted.\n");
        return;
    }
    fprintf(stderr, "  slot advance is by the handler on %s frame%s\n",
            sl_am_slotadv == sl_am_frames ? "EVERY" : "NOT every",
            sl_am_first_slot_bad ? " (first exception at step above)" : "");
    /* ORDERED SEQUENCES. A control is accepted on a change of ORDER here, not
     * on a total moving: these are what "first downstream divergence" is read
     * from. SLOTSEQ is the triple-buffer slot, EVTSEQ the scheduler frameCount
     * the step ran on (the event identity, in the native event domain, never
     * compared against a cartridge frame number), LASTINFOSEQ the slot of the
     * lastInfo handed to osAiSetNextBuffer. */
    fprintf(stderr, "  SLOTSEQ    ");
    for (i = 0; i < sl_am_slot_seq_n; i++) fprintf(stderr, " %lu", sl_am_slot_seq[i]);
    fprintf(stderr, "\n  EVTSEQ     ");
    for (i = 0; i < sl_am_slot_seq_n; i++) fprintf(stderr, " %lu", sl_am_evt_seq[i]);
    fprintf(stderr, "\n  LASTINFOSEQ");
    for (i = 0; i < sl_am_slot_seq_n; i++) fprintf(stderr, " %ld", sl_am_lastinfo_seq[i]);
    fprintf(stderr, "\n");
}
void sl_seq_hdrstate(unsigned div, unsigned off0, unsigned off1)
{
    static int n = 0;
    if (n++ > 0) return;
    fprintf(stderr, "HDRSTATE division=%#x (documented 0x180)  "
                    "trackOffset[0]=%#x (documented 0x44)  trackOffset[1]=%#x\n",
            div, off0, off1);
}
void sl_seq_buffer(const void *buf, unsigned ulen)
{
    const unsigned char *b = (const unsigned char *) buf;
    static int n = 0; unsigned i, nz = 0; int distinct = 0; unsigned char seen0;
    const char *out = getenv("SL_SEQ_DUMP");
    if (n++ > 0) return;
    if (!b || !ulen) { fprintf(stderr, "SEQBUF CAPTURE FAILED: null or zero length\n"); return; }
    seen0 = b[0];
    for (i = 0; i < ulen; i++) { if (b[i]) nz++; if (b[i] != seen0) distinct = 1; }
    if (!nz)      { fprintf(stderr, "SEQBUF CAPTURE FAILED: entirely zero\n"); return; }
    if (!distinct){ fprintf(stderr, "SEQBUF CAPTURE FAILED: single repeated byte\n"); return; }
    fprintf(stderr, "SEQBUF len=%u nonzero=%u first16:", ulen, nz);
    for (i = 0; i < 16 && i < ulen; i++) fprintf(stderr, " %02x", b[i]);
    fprintf(stderr, "\n");
    if (out) { FILE *f = fopen(out, "wb");
               if (f) { fwrite(b, 1, ulen, f); fclose(f);
                        fprintf(stderr, "SEQBUF written to %s\n", out); } }
}
void sl_seam_identity(const char *side, int trk, const void *entryaddr,
                      unsigned ulen, unsigned clen)
{
    static int n = 0;
    if (n++ > 3) return;
    fprintf(stderr, "SEAM[%s] invocation=%d track=%d entryAddr=%p ulen=%u clen=%u\n",
            side, n - 1, trk, entryaddr, ulen, clen);
}
void sl_seq_predecomp(int trk, const void *rom, unsigned ulen, unsigned clen,
                      const void *comp)
{
    const unsigned char *c = (const unsigned char *) comp;
    static int n = 0;
    if (n++ > 2) return;
    fprintf(stderr, "SEQPLAY track=%d romAddr=%p ulen=%u clen=%u\n",
            trk, rom, ulen, clen);
    fprintf(stderr, "  compressed first 16:");
    { int i; for (i = 0; i < 16; i++) fprintf(stderr, " %02x", c[i]); }
    fprintf(stderr, "\n");
}
void sl_seq_postdecomp(const void *seq, unsigned ulen)
{
    const unsigned char *b = (const unsigned char *) seq;
    unsigned be, le; static int n = 0;
    if (n++ > 2) return;
    be = (unsigned)((b[0]<<24)|(b[1]<<16)|(b[2]<<8)|b[3]);
    le = *(const unsigned *) b;
    fprintf(stderr, "  decompressed first 16:");
    { int i; for (i = 0; i < 16; i++) fprintf(stderr, " %02x", b[i]); }
    fprintf(stderr, "\n  ulen=%u  offset-to-data: big-endian=%#x little-endian=%#x"
                    "  (documented: usually 0x44)\n", ulen, be, le);
    /* second documented predicate: the long immediately before the music data
     * is always 0x180. If that holds big-endian too, the container layout is
     * confirmed independently of the first offset. */
    if (be >= 8 && be <= 0x400) {
        const unsigned char *t = b + be - 4;
        unsigned tbe = (unsigned)((t[0]<<24)|(t[1]<<16)|(t[2]<<8)|t[3]);
        unsigned tle = *(const unsigned *) t;
        unsigned k, asc = 1, prev = 0;
        fprintf(stderr, "  long before data: big-endian=%#x little-endian=%#x"
                        "  (documented: always 0x180)\n", tbe, tle);
        for (k = 0; k < be / 4; k++) {
            const unsigned char *e = b + 4 * k;
            unsigned v = (unsigned)((e[0]<<24)|(e[1]<<16)|(e[2]<<8)|e[3]);
            if (k && v < prev) asc = 0;
            prev = v;
        }
        fprintf(stderr, "  %u big-endian longs before data, ascending: %s\n",
                be / 4, asc ? "yes" : "no");
    }
}
/* SLNATV02 carries the EVENT IDENTITY alongside the words, because parity is
 * established on the scheduler/audio event domain before any command word is
 * compared: audioFrame is g_AudioFrameCount (the cartridge census keys on the
 * same counter) and listSlot is g_CurrentAcmdList before its post-dispatch
 * flip. Without those a capture is an unordered bag of command lists and
 * "task 12" means nothing on either side. */
/* Per-task frameSamples, printed HERE because audi.c cannot do I/O (the repo
 * shadows <stdio.h> with a minimal N64 one). Off unless SL_FS_REPORT asks. */
void sl_fs_note(unsigned af, int n)
{
    static int v = -1;
    if (v < 0) { const char *p = getenv("SL_FS_REPORT"); v = (p && *p != '0'); }
    if (v) { extern unsigned int sl_audio_get_length(void);
        fprintf(stderr, "FRAMESAMPLES af=%u n=%d fifo=%u\n",
                af, n, sl_audio_get_length()); }
}
/* WHEN the stage's music is requested, in BOTH clocks at once. music.c can no
 * more do I/O than audi.c can (the repo shadows <stdio.h>), so it hands the
 * values over, exactly as sl_fs_note exists for.
 *
 * Two clocks because one of them cannot separate the candidates. The audio
 * frame is the domain both sides share, and it already says the request is
 * late (cartridge 109, native 249). It cannot say WHY: the audio frame is
 * driven by the pump at a fixed one-per-two-retraces, so "the load spans more
 * retraces" and "the load spans more audio frames" are the same statement.
 * Printing the pumped frame alongside makes the ratio checkable rather than
 * assumed. */
void sl_music_note(unsigned track, unsigned af)
{
    static int v = -1;
    if (v < 0) { const char *p = getenv("SL_MUSIC_REPORT"); v = (p && *p != '0'); }
    if (v) { extern unsigned sl_frames_completed(void);
        fprintf(stderr, "MUSICREQ track=%u audioFrame=%u pumpedFrame=%u\n",
                track, af, sl_frames_completed()); }
}
void sl_acmd_capture(const void *list, int ncmds, unsigned audioFrame,
                     unsigned listSlot)
{
    static FILE *f = 0; static int opened = 0; static long tasks = 0;
    unsigned int hdr[4];
    if (!opened) {
        const char *p = getenv("SL_ACMD_OUT");
        opened = 1;
        if (p) { f = fopen(p, "wb"); if (f) fwrite("SLNATV02", 1, 8, f); }
    }
    if (!f || ncmds < 0) return;
    hdr[0] = (unsigned int) tasks++;
    hdr[1] = (unsigned int) ncmds;
    hdr[2] = audioFrame;
    hdr[3] = listSlot;
    fwrite(hdr, 4, 4, f);
    if (ncmds > 0) fwrite(list, 8, (size_t) ncmds, f);
    fflush(f);
}
void sl_audio_counts(int seq, void *a, void *b, void *c)
{
    fprintf(stderr, "AUDIOCOUNTS sequences=%d seqPlayers=%d of 3\n",
            seq, (a != 0) + (b != 0) + (c != 0));
}
void sl_seq_entries(const void *tbl, int n)
{
    /* first, middle and last entries, as the table stands after whatever
     * normalisation mode ran. Ranges are checkable against the documented
     * table description; "looks plausible" is not an oracle. */
    const unsigned char *b = (const unsigned char *) tbl;
    int idx[3], k;
    fprintf(stderr, "SEQENTRIES count=%d\n", n);
    if (n <= 0) return;
    idx[0] = 0; idx[1] = n / 2; idx[2] = n - 1;
    for (k = 0; k < 3; k++) {
        const unsigned char *e = b + 4 + 8 * idx[k];
        unsigned addr = *(const unsigned *) e;
        unsigned ulen = *(const unsigned short *) (e + 4);
        unsigned clen = *(const unsigned short *) (e + 6);
        fprintf(stderr, "  entry %-3d addr=%-12u ulen=%-8u clen=%-8u\n",
                idx[k], addr, ulen, clen);
    }
}
void sl_alheap_report(void)
{
    long i, tot = 0;
    if (!sl_alheap_n) return;
    fprintf(stderr, "\n=== AUDIO HEAP DEMAND (%ld requests) ===\n", sl_alheap_n);
    for (i = 0; i < sl_alheap_n; i++) {
        tot += sl_alheap_req[i];
        fprintf(stderr, "  %-3s req %8ld  used %8ld  len %8ld  free %8ld  cum %8ld  %s:%ld\n",
                sl_alheap_ok[i] ? "ok" : "REF", sl_alheap_req[i], sl_alheap_used[i],
                sl_alheap_len[i], sl_alheap_len[i] - sl_alheap_used[i], tot,
                sl_alheap_file[i], sl_alheap_line[i]);
    }
    fprintf(stderr, "  TOTAL REQUESTED %ld bytes across %ld requests\n", tot, sl_alheap_n);
}
#endif /* SL_ALHEAP_TRACE */

static void sl_say_hex(const char *label, unsigned long v)
{
    static const char hex[] = "0123456789abcdef";
    char b[32];
    int i, n = 0;
    sl_say(label);
    b[n++] = '0'; b[n++] = 'x';
    for (i = 28; i >= 0; i -= 4)
        b[n++] = hex[(v >> i) & 0xf];
    b[n++] = '\n'; b[n] = 0;
    sl_say(b);
}

#ifdef _WIN32
/* CRASH-REPORT SAFETY AND DETAIL. Added 2026-09-02 while diagnosing B-063.
 *
 * MEASURED, and the reason this exists: the reporter was destroying the report
 * it exists to produce. sl_report walks the Ebp chain guarded only by a range
 * test, which says nothing about whether the memory is mapped. On the B-063
 * fault Ebp did not belong to a live frame, the walk printed one garbage value
 * and then faulted at sl_report+0x66 - and that second fault was reported as a
 * second CRASH, which read like a second defect in the game. It was the
 * instrument breaking, not the subject.
 */
static int sl_win_readable(const void *p, unsigned long n)
{
    MEMORY_BASIC_INFORMATION mbi;
    const unsigned char *b = (const unsigned char *) p;
    if (VirtualQuery(p, &mbi, sizeof mbi) != sizeof mbi)
        return 0;
    if (mbi.State != MEM_COMMIT)
        return 0;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))
        return 0;
    if (b + n > (const unsigned char *) mbi.BaseAddress + mbi.RegionSize)
        return 0;
    return 1;
}

static int sl_win_image_range(unsigned long *lo, unsigned long *hi)
{
    unsigned char *base = (unsigned char *) GetModuleHandleA(NULL);
    IMAGE_DOS_HEADER *dos;
    IMAGE_NT_HEADERS *nt;
    if (base == NULL)
        return 0;
    dos = (IMAGE_DOS_HEADER *) base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return 0;
    nt = (IMAGE_NT_HEADERS *) (base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return 0;
    *lo = (unsigned long) base;
    *hi = *lo + nt->OptionalHeader.SizeOfImage;
    return 1;
}

/* A frame-pointer chain is worthless once Ebp is not a frame. Scanning the
 * stack for words that fall inside this image's own address range recovers the
 * call path anyway, at the cost of some false positives - which is the right
 * trade for a report that otherwise says nothing. */
static void sl_win_stack_scan(unsigned long esp)
{
    const unsigned long *sp = (const unsigned long *) esp;
    unsigned long lo = 0, hi = 0;
    int i, shown = 0;

    if (!sl_win_image_range(&lo, &hi))
        return;
    sl_say("  stack words inside the image (candidate return addresses):\n");
    for (i = 0; i < 512 && shown < 16; i++) {
        if (!sl_win_readable(sp + i, sizeof *sp))
            break;
        if (sp[i] >= lo && sp[i] < hi) {
            sl_say_hex("    ", sp[i]);
            shown++;
        }
    }
    if (shown == 0)
        sl_say("    <none - the stack holds no address inside this image>\n");
}
#endif /* _WIN32 */

#ifndef _WIN32
/* No VirtualQuery here; mincore would need a page-aligned probe and this runs
 * inside a signal handler. A plausibility test keeps the pose writer from
 * following NULL or a low garbage word; a genuinely unmapped pointer would
 * fault the reporter, which the SIGSEGV re-raise below already tolerates. */
static int sl_posix_plausible(const void *p, unsigned long n)
{
    (void) n;
    return p != NULL && (unsigned long) p >= 0x10000ul;
}
#endif

static void sl_report(const char *what, unsigned long ip, unsigned long bp)
{
    int i;
    sl_say("\n=== sightline ");
    sl_say(what);
    sl_say(" ===\n");
    sl_say_hex("  frame  ", (unsigned long) sl_frames_completed());
    sl_say_hex("  pc     ", ip);
    for (i = 0; i < 16 && bp > 0x1000 && bp < 0xffffe000ul; i++) {
        unsigned long *f = (unsigned long *) bp;
#ifdef _WIN32
        if (!sl_win_readable(f, 2 * sizeof *f)) {
            sl_say("  ra     <frame pointer unreadable - walk stops here>\n");
            break;
        }
#endif
        sl_say_hex("  ra     ", f[1]);
        if (f[0] <= bp) break;
        bp = f[0];
    }
    sl_say("  resolve with: tools/native/crashinfo.sh\n");
    /* B-139. WHERE THE PLAYER WAS, in the F8 mark's own [teleport] form, so
     * the report is a teleport anchor: play.ps1 -Level <name> -TeleportMark
     * <run>\crash.txt. The game side (src/native/sl_game_query.c) reads the
     * player through the readability predicate this reporter already trusts
     * for its own stack walk, so a crash that corrupted the player prints
     * UNKNOWN rather than faulting the reporter. */
    {
        extern int sl_crash_pose(char *out, int n,
                                 int (*readable)(const void *, unsigned long));
        static char pose[512];
#ifdef _WIN32
        if (sl_crash_pose(pose, (int) sizeof pose, sl_win_readable) > 0)
#else
        if (sl_crash_pose(pose, (int) sizeof pose, sl_posix_plausible) > 0)
#endif
            sl_say(pose);
    }
    /* WHICH CHARACTER RECORD, if one holds a prop pointer outside g_Props
     * (src/native/sl_chr_watch.c): the 2026-09-17 Aztec report could name the
     * consumer and the value but not the record, which is a local three
     * frames up. Same readability predicate, same terms. */
    {
        extern int sl_crash_chr_scan(char *out, int n,
                                     int (*readable)(const void *, unsigned long));
        static char chrs[2048];
#ifdef _WIN32
        if (sl_crash_chr_scan(chrs, (int) sizeof chrs, sl_win_readable) > 0)
#else
        if (sl_crash_chr_scan(chrs, (int) sizeof chrs, sl_posix_plausible) > 0)
#endif
            sl_say(chrs);
    }
}

#ifndef _WIN32
static void sl_on_segv(int sig, siginfo_t *si, void *uc_)
{
    ucontext_t *uc = (ucontext_t *) uc_;
    if (sl_pager_fault((unsigned long) si->si_addr))
        return;                      /* paged in - retry the instruction */
    sl_say_hex("\nsightline native: SIGSEGV addr ", (unsigned long) si->si_addr);
#ifdef SL_ALHEAP_TRACE
    sl_alheap_report();
#endif
    sl_report("CRASH", (unsigned long) uc->uc_mcontext.gregs[REG_EIP],
              (unsigned long) uc->uc_mcontext.gregs[REG_EBP]);
    /* Re-raise with the DEFAULT handler rather than _exit(): that is what
     * produces a core wherever the environment allows one, and _exit() is
     * precisely why this project never got one. Costs nothing when it does
     * not. */
    signal(sig, SIG_DFL);
    raise(sig);
    _exit(5);
}
#endif /* !_WIN32 */

/* Stalled-frame budget. A wall-clock deadline cannot tell a hang from someone
 * reading the briefing, so the watchdog fires only when the pump has stopped
 * advancing - which is what an allocator spin or a game-loop hang looks like,
 * and what a long session does not. */
/* 10, not 30. The first real hang went undiagnosed because the window was
 * closed before a 30-second budget expired - and with nothing printed while it
 * counted, there was no way to know waiting would have helped. Ten seconds of
 * a frozen frame pump is already unambiguous. */
static unsigned g_hang_budget = 10;
static unsigned g_hang_stalled;
static unsigned g_hang_lastframe;

#ifndef _WIN32
static void sl_on_alarm(int sig, siginfo_t *si, void *uc_)
{
    ucontext_t *uc = (ucontext_t *) uc_;
    (void) sig; (void) si;

    if (sl_frames_completed() != g_hang_lastframe) {   /* progress: healthy, keep watching */
        g_hang_lastframe = sl_frames_completed();
        g_hang_stalled = 0;
        alarm(1);
        return;
    }
    if (++g_hang_stalled < g_hang_budget) {
        /* Say so while counting. Silence here cost a diagnosis: the frame pump
         * had already stopped and nothing on screen or in the log suggested
         * that holding on a few more seconds would produce a report. */
        if (g_hang_stalled == 3) {
            sl_say("sightline native: frame pump has stalled - hold on, a hang "
                   "report is coming.\n");
        }
        alarm(1);
        return;
    }
    sl_say("\nsightline native: HANG - the frame pump stopped advancing.\n");
    sl_say("  The allocator spins on `while (1);` when a pool is exhausted\n"
           "  (memp.c), so check the pc below against that first.\n");
    sl_report("HANG", (unsigned long) uc->uc_mcontext.gregs[REG_EIP],
              (unsigned long) uc->uc_mcontext.gregs[REG_EBP]);
    /* abort() rather than _exit(): raises SIGABRT with default disposition, so
     * a core lands wherever the environment permits one. */
    signal(SIGABRT, SIG_DFL);
    abort();
}
#endif /* !_WIN32 */

#ifdef _WIN32
/* ---- Win32 fault and stall reporting -------------------------------------
 * Windows has neither sigaction nor ucontext, so both reporters are rebuilt
 * on the platform's own primitives rather than stubbed out. They are this
 * project's primary diagnostic surface; a silent one would be worse than
 * none.
 *
 * The fault path is a vectored exception handler, which runs BEFORE any SEH
 * frame and so sees the access violation before SDL or the GL driver can
 * swallow it. EXCEPTION_CONTINUE_EXECUTION restarts the faulting
 * instruction, which is exactly what returning from the POSIX SIGSEGV
 * handler does - that is what makes the cartridge pager work here.
 *
 * The stall path is a thread rather than a timer signal. To recover the
 * stalled pc and frame pointer (what uc_mcontext.gregs supplies on Linux)
 * the watchdog must SuspendThread the pump first: GetThreadContext on a
 * running thread returns nothing meaningful.
 */
static LONG CALLBACK sl_win_veh(EXCEPTION_POINTERS *ep)
{
    unsigned long addr;

    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION)
        return EXCEPTION_CONTINUE_SEARCH;
    addr = (unsigned long) ep->ExceptionRecord->ExceptionInformation[1];
    if (sl_pager_fault(addr))
        return EXCEPTION_CONTINUE_EXECUTION;
    sl_say_hex("\nsightline native: ACCESS VIOLATION addr ", addr);

    {
        /* ExceptionInformation[0] is the ONE field that distinguishes a bad
         * data access from a jump to a non-code address, and it was never
         * printed. Without it the B-063 report was ambiguous between "read a
         * wild pointer" and "called through one", which are different bugs. */
        unsigned long kind = (unsigned long) ep->ExceptionRecord->ExceptionInformation[0];
        sl_say(kind == 0 ? "  access: READ\n" :
               kind == 1 ? "  access: WRITE\n" :
               kind == 8 ? "  access: EXECUTE - control was transferred to a non-code address\n" :
                           "  access: (unrecognised)\n");
        sl_say_hex("  eip    ", (unsigned long) ep->ContextRecord->Eip);
        sl_say_hex("  esp    ", (unsigned long) ep->ContextRecord->Esp);
        sl_say_hex("  ebp    ", (unsigned long) ep->ContextRecord->Ebp);
        /* B-139. WHAT WINDOWS SAYS LIVES AT THE FAULT ADDRESS. The Aztec
         * crash read 0x04000000 - an N64 segment address consumed as a host
         * pointer - and the question that decided the diagnosis was whether
         * that page was free, reserved or committed, because the walker's
         * readability probe had accepted it. The kernel's answer costs one
         * VirtualQuery and settles it in the report itself. */
        {
            MEMORY_BASIC_INFORMATION mbi;
            char b[200];
            if (VirtualQuery((void *) addr, &mbi, sizeof mbi) == sizeof mbi) {
                snprintf(b, sizeof b,
                         "  region at addr: base 0x%08lx alloc 0x%08lx size 0x%08lx"
                         " state %s protect 0x%lx type 0x%lx\n",
                         (unsigned long) mbi.BaseAddress,
                         (unsigned long) mbi.AllocationBase,
                         (unsigned long) mbi.RegionSize,
                         mbi.State == MEM_FREE ? "FREE" :
                         mbi.State == MEM_RESERVE ? "RESERVE (not committed - a read faults)" :
                         mbi.State == MEM_COMMIT ? "COMMIT" : "?",
                         (unsigned long) mbi.Protect, (unsigned long) mbi.Type);
                sl_say(b);
            } else {
                sl_say("  region at addr: VirtualQuery failed\n");
            }
        }
        /* On an EXECUTE fault the target instruction never ran, so the CALL's
         * pushed return address is still sitting exactly at [esp]. That IS the
         * call site - the one thing an Ebp chain cannot give here, because Ebp
         * still belongs to the caller and never became a frame at the target. */
        if (kind == 8) {
            const unsigned long *sp = (const unsigned long *) ep->ContextRecord->Esp;
            if (sl_win_readable(sp, sizeof *sp))
                sl_say_hex("  call site (return address at [esp]) ", sp[0]);
            else
                sl_say("  call site: [esp] unreadable\n");
        }
        sl_win_stack_scan((unsigned long) ep->ContextRecord->Esp);
    }
#ifdef SL_ALHEAP_TRACE
    sl_alheap_report();
#endif
    sl_report("CRASH", (unsigned long) ep->ContextRecord->Eip,
              (unsigned long) ep->ContextRecord->Ebp);
    /* Hand it on: the default handler terminates the process, which is the
     * Windows counterpart of re-raising with SIG_DFL. */
    return EXCEPTION_CONTINUE_SEARCH;
}

static HANDLE g_hang_pump;          /* the thread that runs the frame pump */

static DWORD WINAPI sl_win_watchdog(LPVOID arg)
{
    (void) arg;
    for (;;) {
        Sleep(1000);
        if (sl_frames_completed() != g_hang_lastframe) {
            g_hang_lastframe = sl_frames_completed();
            g_hang_stalled = 0;
            continue;
        }
        if (++g_hang_stalled < g_hang_budget) {
            if (g_hang_stalled == 3)
                sl_say("sightline native: frame pump has stalled - hold on, a "
                       "hang report is coming.\n");
            continue;
        }
        sl_say("\nsightline native: HANG - the frame pump stopped advancing.\n");
        sl_say("  The allocator spins on `while (1);` when a pool is exhausted\n"
               "  (memp.c), so check the pc below against that first.\n");
        {
            CONTEXT c;
            memset(&c, 0, sizeof c);
            c.ContextFlags = CONTEXT_CONTROL;
            SuspendThread(g_hang_pump);
            if (GetThreadContext(g_hang_pump, &c))
                sl_report("HANG", (unsigned long) c.Eip,
                          (unsigned long) c.Ebp);
            else
                sl_report("HANG", 0, 0);
            ResumeThread(g_hang_pump);
        }
        signal(SIGABRT, SIG_DFL);
        abort();
    }
}
#endif /* _WIN32 */

int main(void)
{
    const char *hang = getenv("SL_HANG_SECONDS");

#ifdef _WIN32
    /* SDL WINDOWS MAIN - the decision, recorded where it is made.
     *
     * SDL2 on Windows normally redirects main() to SDL_main via SDL2main,
     * which supplies its own WinMain and expects int main(int, char **).
     * This program takes NO command-line arguments, owns its startup ordering
     * deliberately (the window must come up before sl_pager_init reserves the
     * kseg2 window, see below), and depends on stderr reaching a console for
     * every diagnostic it prints.
     *
     * SDL2main fights all three: it changes the entry signature, it takes over
     * startup, and pkg-config pairs it with -mwindows, which detaches the
     * console and silently discards the whole reporting surface this file
     * exists to provide.
     *
     * So: SDL_MAIN_HANDLED, no -lSDL2main, no -mwindows, and this main() stays
     * the true entry point, unchanged from the POSIX build. SDL_SetMainReady()
     * is its required counterpart - without it SDL_InitSubSystem(SDL_INIT_VIDEO)
     * refuses to start, and the failure reads like a video problem rather than
     * a main-handling one. */
    SDL_SetMainReady();

    /* Keep failures on the console rather than on the owner's screen. An
     * aborting process otherwise raises Windows Error Reporting, and the hang
     * and crash reporters below both end in abort() - so the one path that
     * runs when something has already gone wrong is exactly the path that
     * would pop a modal dialog. */
    SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
#endif

    /* Let ANY process of this user attach with gdb, despite ptrace_scope=1.
     * Without it only a parent may attach, so a hung window is a dead end for
     * everyone - which is exactly what happened on 2026-08-25: the game sat
     * spinning at 100% CPU and nothing could read its stack, not even its own
     * instruction pointer via /proc. One syscall, no cost, and it turns
     * "the window is still open" into `gdb -p <pid>`. */
#ifdef PR_SET_PTRACER
    prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0);
#endif
    sl_run_open();
#ifndef _WIN32
    /* No SIGUSR1 on Windows. F9 still marks, which is the path a person
     * actually uses; only the `kill -USR1` route is unavailable. */
    signal(SIGUSR1, sl_on_mark_signal);
#endif
    sl_crash_open();
    sl_shim_configure();
    /* Window FIRST: SDL/X11/GL install their own mappings, and the skeleton's
     * pager reserves address space PROT_NONE while its SIGSEGV handler owns
     * faults. A standalone 32-bit SDL GL window succeeds, so ordering is the
     * difference. */
    if (sl_gfx_init())
        sl_gfx_begin_frame();
#ifdef _WIN32
    /* First handler in the chain, so the pager sees the fault before SDL or
     * the GL driver installs anything of its own. */
    AddVectoredExceptionHandler(1, sl_win_veh);
#else
    {
        struct sigaction sg = {0};
        sg.sa_sigaction = sl_on_segv;
        sg.sa_flags = SA_SIGINFO;
        sigaction(SIGSEGV, &sg, 0);
    }
#endif
    /* Default ON. It was opt-in before and so was never armed when it was
     * needed. 0 disables. */
    if (hang == NULL || atoi(hang) > 0) {
#ifdef _WIN32
        if (hang != NULL)
            g_hang_budget = (unsigned) atoi(hang);
        g_hang_lastframe = sl_frames_completed();
        /* A real handle on THIS thread: the GetCurrentThread() pseudo-handle
         * means "the calling thread" and would resolve to the watchdog
         * itself once handed across. */
        DuplicateHandle(GetCurrentProcess(), GetCurrentThread(),
                        GetCurrentProcess(), &g_hang_pump,
                        0, FALSE, DUPLICATE_SAME_ACCESS);
        CloseHandle(CreateThread(NULL, 0, sl_win_watchdog, NULL, 0, NULL));
#else
        struct sigaction sa = {0};
        if (hang != NULL)
            g_hang_budget = (unsigned) atoi(hang);
        sa.sa_sigaction = sl_on_alarm;
        sa.sa_flags = SA_SIGINFO;
        sigaction(SIGALRM, &sa, 0);
        g_hang_lastframe = sl_frames_completed();
        alarm(1);
#endif
        fprintf(stderr, "sightline native: hang watchdog armed (%u stalled "
                        "seconds)\n", g_hang_budget);
    }
    sl_pager_init();

    /* SL_SELFTEST exercises the diagnostic paths on demand. A reporting system
     * nobody has seen fire is worth very little - the previous one had a hang
     * watchdog that was opt-in, never armed, and therefore never once ran when
     * a hang actually happened. This makes both paths provable in seconds:
     *
     *   SL_SELFTEST=crash   deliberate null write -> the CRASH report
     *   SL_SELFTEST=hang    deliberate spin       -> the HANG report
     *
     * Neither is reachable without the variable set. */
    {
        const char *st = getenv("SL_SELFTEST");
        if (st != NULL && strcmp(st, "crash") == 0) {
            fprintf(stderr, "sightline native: SELFTEST crash\n");
            *(volatile int *) 0 = 1;
        }
        if (st != NULL && strcmp(st, "hang") == 0) {
            fprintf(stderr, "sightline native: SELFTEST hang - spinning\n");
            for (;;) { }
        }
    }

    /* The audio microcode's data tables come out of the user's ROM here, on
     * this thread, before anything can build or run an audio command list
     * (sl_ucode.c). Not derivable = not bootable, with the reason on stderr;
     * exit 3 is the code sl_rom_load already uses for a ROM it cannot open. */
    if (sl_ucode_tables_derive() != 0)
        return 3;

    fprintf(stderr, "sightline native: booting via mainproc\n");
    mainproc(0);
    return 1;
}
