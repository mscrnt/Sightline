/* Sightline deterministic input plugin for mupen64plus.
 *
 * Two modes, selected by environment variable:
 *
 *   SL_INPUT_REPLAY=<file>   feed recorded controller state back to the game.
 *                            No hardware involved, so this works headless and
 *                            in CI - which is the whole point: verification
 *                            must never depend on a controller being attached.
 *
 *   SL_INPUT_RECORD=<file>   load the real SDL input plugin, forward every call
 *                            to it, and log what it returned. Used on a machine
 *                            where the controller actually works.
 *
 * The stream is indexed by CONTROLLER READ COUNT, not by game tick. The core
 * calls GetKeys when the game polls the controller; because both emulator and
 * game are deterministic, the Nth read during replay corresponds to the Nth
 * read during recording. Indexing by tick would be wrong - the game does not
 * poll exactly once per tick.
 *
 * Format: 4-byte big-endian BUTTONS.Value per read, controller 0 only.
 * Controllers 1-3 are reported absent (single player).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

#include "m64p_types.h"
#include "m64p_plugin.h"

#define SL_VERSION 0x000100
#define SL_API_VERSION 0x020100

static void *l_real_plugin = NULL;          /* SDL plugin, record mode only */
static ptr_GetKeys l_real_getkeys = NULL;
static ptr_InitiateControllers l_real_initiate = NULL;
static ptr_ControllerCommand l_real_command = NULL;
static ptr_ReadController l_real_read = NULL;
static ptr_RomOpen l_real_romopen = NULL;
static ptr_RomClosed l_real_romclosed = NULL;
static ptr_SDL_KeyDown l_real_keydown = NULL;
static ptr_SDL_KeyUp l_real_keyup = NULL;
static FILE *l_stream = NULL;
static int l_recording = 0;
static int l_chaining = 0;          /* forwarding to the real SDL plugin */
static int l_invert_x = 0;
static int l_invert_y = 0;
static double l_mouse_sens = 1.5;
static int l_mouse_look = 1;
static int l_eof_reported = 0;
static unsigned long l_reads = 0;
static CONTROL *l_controls = NULL;

/* SDL entry points, resolved from the already-loaded library. The SDL input
 * plugin arms relative mouse mode once at startup and never re-arms it, so
 * after the window loses focus the mouse stops driving look while the buttons
 * keep working. Re-arm it whenever the window has focus and grab has lapsed. */
static unsigned (*l_sdl_get_rel_state)(int *, int *) = NULL;
static void (*l_sdl_warp)(void *, int, int) = NULL;
static void (*l_sdl_get_window_size)(void *, int *, int *) = NULL;
static void (*l_sdl_show_cursor)(int) = NULL;
static int  (*l_sdl_get_relative)(void) = NULL;
static int  (*l_sdl_set_relative)(int) = NULL;
static void *(*l_sdl_keyboard_focus)(void) = NULL;
static int  l_sdl_resolved = 0;

static void (*l_debug)(void *, int, const char *) = NULL;
static void *l_debug_ctx = NULL;

static void resolve_sdl(void)
{
    if (l_sdl_resolved) return;
    l_sdl_resolved = 1;
    *(void **)&l_sdl_get_relative = dlsym(RTLD_DEFAULT, "SDL_GetRelativeMouseMode");
    *(void **)&l_sdl_set_relative = dlsym(RTLD_DEFAULT, "SDL_SetRelativeMouseMode");
    *(void **)&l_sdl_keyboard_focus = dlsym(RTLD_DEFAULT, "SDL_GetKeyboardFocus");
    *(void **)&l_sdl_get_rel_state = dlsym(RTLD_DEFAULT, "SDL_GetRelativeMouseState");
    *(void **)&l_sdl_warp = dlsym(RTLD_DEFAULT, "SDL_WarpMouseInWindow");
    *(void **)&l_sdl_get_window_size = dlsym(RTLD_DEFAULT, "SDL_GetWindowSize");
    *(void **)&l_sdl_show_cursor = dlsym(RTLD_DEFAULT, "SDL_ShowCursor");
}

/* Drive the analog axes ourselves from relative mouse motion.
 *
 * The SDL input plugin's own mouse path does not hold capture reliably here:
 * motion stops after focus is lost and the cursor is never confined to the
 * window. Reading motion directly gives us capture, centring, sensitivity and
 * sign in one place - and it is where a sensitivity curve would go later.
 *
 * The N64 stick saturates around +/-80, so that is the clamp.
 */
static void apply_mouse_look(BUTTONS *Keys)
{
    void *win;
    int dx = 0, dy = 0, w = 0, h = 0, x, y;

    if (!l_mouse_look) return;          /* a real stick is driving the axes */
    resolve_sdl();
    if (!l_sdl_get_rel_state || !l_sdl_keyboard_focus) return;

    win = l_sdl_keyboard_focus();
    if (!win) return;                       /* unfocused: leave the cursor free */

    if (l_sdl_set_relative && l_sdl_get_relative && !l_sdl_get_relative())
        l_sdl_set_relative(1);
    if (l_sdl_show_cursor) l_sdl_show_cursor(0);

    l_sdl_get_rel_state(&dx, &dy);
    if (dx == 0 && dy == 0) return;

    x = (int)(dx * l_mouse_sens);
    y = (int)(dy * l_mouse_sens);
    if (l_invert_x) x = -x;
    if (l_invert_y) y = -y;
    if (x > 80) x = 80; else if (x < -80) x = -80;
    if (y > 80) y = 80; else if (y < -80) y = -80;

    Keys->X_AXIS = x;
    Keys->Y_AXIS = y;

    /* Belt and braces: if relative mode is unavailable, keep the pointer pinned
     * to the centre so it cannot wander out of the window. */
    if (l_sdl_warp && l_sdl_get_window_size && l_sdl_get_relative &&
        !l_sdl_get_relative()) {
        l_sdl_get_window_size(win, &w, &h);
        if (w > 0 && h > 0) l_sdl_warp(win, w / 2, h / 2);
    }
}

static void sl_log(int level, const char *msg)
{
    if (l_debug) l_debug(l_debug_ctx, level, msg);
}

/* ---- common plugin interface ------------------------------------------ */

EXPORT m64p_error CALL PluginGetVersion(m64p_plugin_type *type, int *version,
                                        int *api, const char **name, int *caps)
{
    if (type) *type = M64PLUGIN_INPUT;
    if (version) *version = SL_VERSION;
    if (api) *api = SL_API_VERSION;
    if (name) *name = "Sightline deterministic input";
    if (caps) *caps = 0;
    return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL PluginStartup(m64p_dynlib_handle CoreHandle, void *Context,
                                     void (*DebugCallback)(void *, int, const char *))
{
    const char *replay = getenv("SL_INPUT_REPLAY");
    const char *record = getenv("SL_INPUT_RECORD");
    const char *inv_x = getenv("SL_INVERT_X");
    const char *inv_y = getenv("SL_INVERT_Y");
    char buf[512];

    /* Axis inversion lives here rather than in the SDL plugin's config: a
     * negative MouseSensitivity disables mouse motion in that plugin instead of
     * inverting it. Default on, because mouse look comes out mirrored on both
     * axes against GoldenEye's analog aim. */
    l_invert_x = (inv_x == NULL) ? 1 : (*inv_x == '1' || *inv_x == 't' || *inv_x == 'T');
    l_invert_y = (inv_y == NULL) ? 1 : (*inv_y == '1' || *inv_y == 't' || *inv_y == 'T');
    {
        const char *ml = getenv("SL_MOUSE_LOOK");
        if (ml && (*ml == '0' || *ml == 'f' || *ml == 'F')) l_mouse_look = 0;
    }
    {
        const char *sens = getenv("SL_MOUSE_SENS");
        if (sens && *sens) {
            double v = atof(sens);
            if (v > 0.0) l_mouse_sens = v;
        }
    }

    l_debug = DebugCallback;
    l_debug_ctx = Context;
    l_reads = 0;
    l_eof_reported = 0;

    if (replay && *replay) {
        l_stream = fopen(replay, "rb");
        if (!l_stream) {
            snprintf(buf, sizeof buf, "slinput: cannot open replay stream %s", replay);
            sl_log(M64MSG_ERROR, buf);
            return M64ERR_FILES;
        }
        l_recording = 0;
        snprintf(buf, sizeof buf, "slinput: replaying %s", replay);
        sl_log(M64MSG_INFO, buf);
        return M64ERR_SUCCESS;
    }

    if (1) {
        /* Not replaying: chain to the stock SDL plugin so a human can play.
         * Recording is then just "also write down what it returned". */
        const char *sdl = getenv("SL_INPUT_SDL_PLUGIN");
        if (!sdl || !*sdl)
            sdl = "/usr/lib/x86_64-linux-gnu/mupen64plus/mupen64plus-input-sdl.so";
        l_real_plugin = dlopen(sdl, RTLD_NOW | RTLD_LOCAL);
        if (!l_real_plugin) {
            snprintf(buf, sizeof buf, "slinput: cannot load %s: %s", sdl, dlerror());
            sl_log(M64MSG_ERROR, buf);
            return M64ERR_FILES;
        }
        m64p_error (*real_startup)(m64p_dynlib_handle, void *,
                                   void (*)(void *, int, const char *));
        *(void **)&real_startup = dlsym(l_real_plugin, "PluginStartup");
        *(void **)&l_real_getkeys = dlsym(l_real_plugin, "GetKeys");
        /* Forward the whole interface, not just GetKeys. InitiateControllers in
         * particular is where the SDL plugin opens the joystick and reads its
         * config - skipping it leaves the pad unopened while GetKeys still
         * appears to "work", which is exactly as confusing as it sounds. */
        *(void **)&l_real_initiate = dlsym(l_real_plugin, "InitiateControllers");
        *(void **)&l_real_command = dlsym(l_real_plugin, "ControllerCommand");
        *(void **)&l_real_read = dlsym(l_real_plugin, "ReadController");
        *(void **)&l_real_romopen = dlsym(l_real_plugin, "RomOpen");
        *(void **)&l_real_romclosed = dlsym(l_real_plugin, "RomClosed");
        *(void **)&l_real_keydown = dlsym(l_real_plugin, "SDL_KeyDown");
        *(void **)&l_real_keyup = dlsym(l_real_plugin, "SDL_KeyUp");
        if (!real_startup || !l_real_getkeys) {
            sl_log(M64MSG_ERROR, "slinput: SDL plugin missing entry points");
            return M64ERR_FILES;
        }
        real_startup(CoreHandle, Context, DebugCallback);

        l_chaining = 1;

        if (record && *record) {
            l_stream = fopen(record, "wb");
            if (!l_stream) {
                snprintf(buf, sizeof buf, "slinput: cannot open record stream %s",
                         record);
                sl_log(M64MSG_ERROR, buf);
                return M64ERR_FILES;
            }
            l_recording = 1;
            snprintf(buf, sizeof buf, "slinput: recording to %s", record);
            sl_log(M64MSG_INFO, buf);
        } else {
            sl_log(M64MSG_INFO, "slinput: passthrough (not recording)");
        }
        snprintf(buf, sizeof buf, "slinput: invert_x=%d invert_y=%d",
                 l_invert_x, l_invert_y);
        sl_log(M64MSG_INFO, buf);
        return M64ERR_SUCCESS;
    }
}

EXPORT m64p_error CALL PluginShutdown(void)
{
    if (l_stream) { fclose(l_stream); l_stream = NULL; }
    if (l_real_plugin) {
        void (*real_shutdown)(void);
        *(void **)&real_shutdown = dlsym(l_real_plugin, "PluginShutdown");
        if (real_shutdown) real_shutdown();
        dlclose(l_real_plugin);
        l_real_plugin = NULL;
    }
    return M64ERR_SUCCESS;
}

/* ---- input interface --------------------------------------------------- */

EXPORT void CALL InitiateControllers(CONTROL_INFO ControlInfo)
{
    l_controls = ControlInfo.Controls;

    if (l_chaining && l_real_initiate) {
        /* Let the SDL plugin open the device and apply its config. */
        l_real_initiate(ControlInfo);
        /* Pin the controller count regardless: a varying number of connected
         * pads changes PIF traffic, and replay must reproduce it exactly. */
        for (int i = 1; i < 4; i++) l_controls[i].Present = 0;
        l_controls[0].Present = 1;
        return;
    }

    /* Replay: no hardware involved, so declare exactly the shape record had.
     *
     * This must match the recording config's "plugin" setting. GoldenEye probes
     * the controller pak, so answering PLUGIN_NONE on replay when recording
     * answered PLUGIN_MEMPAK changes PIF traffic and diverges on the very first
     * poll - state differs at tick 1 while tick and VI counts still line up
     * exactly, which looks nothing like an input desync. */
    for (int i = 0; i < 4; i++) {
        l_controls[i].Present = (i == 0) ? 1 : 0;
        l_controls[i].RawData = 0;
        l_controls[i].Plugin = (i == 0) ? PLUGIN_MEMPAK : PLUGIN_NONE;
    }
}

EXPORT void CALL GetKeys(int Control, BUTTONS *Keys)
{
    unsigned char b[4];

    if (!Keys) return;
    Keys->Value = 0;
    if (Control != 0) return;

    if (l_chaining) {
        if (l_real_getkeys) l_real_getkeys(Control, Keys);
        apply_mouse_look(Keys);

        if (!l_recording) return;

        b[0] = (Keys->Value >> 24) & 0xFF;
        b[1] = (Keys->Value >> 16) & 0xFF;
        b[2] = (Keys->Value >> 8) & 0xFF;
        b[3] = Keys->Value & 0xFF;
        if (l_stream) {
            fwrite(b, 1, 4, l_stream);
            /* Flush every read rather than relying on a clean shutdown. There
             * is no in-game way to quit the harness - the window gets closed,
             * and a killed process would lose whatever sat in the stdio buffer.
             * A recording that silently loses its tail is worse than a slow
             * one, and 4 bytes per poll is nothing next to emulating a frame. */
            fflush(l_stream);
        }
        l_reads++;
        return;
    }

    if (!l_stream) return;

    if (fread(b, 1, 4, l_stream) != 4) {
        /* Past the end of the recording: hold neutral. Report once so a trace
         * that outlives its input stream is visible rather than silent. */
        if (!l_eof_reported) {
            char buf[128];
            snprintf(buf, sizeof buf,
                     "slinput: input stream exhausted after %lu reads; "
                     "holding neutral input", l_reads);
            sl_log(M64MSG_WARNING, buf);
            l_eof_reported = 1;
        }
        Keys->Value = 0;
        return;
    }
    Keys->Value = ((unsigned int)b[0] << 24) | ((unsigned int)b[1] << 16) |
                  ((unsigned int)b[2] << 8) | (unsigned int)b[3];
    l_reads++;
}

EXPORT void CALL ControllerCommand(int Control, unsigned char *Command)
{
    if (l_chaining && l_real_command) l_real_command(Control, Command);
}

EXPORT void CALL ReadController(int Control, unsigned char *Command)
{
    if (l_chaining && l_real_read) l_real_read(Control, Command);
}

EXPORT int CALL RomOpen(void)
{
    if (l_chaining && l_real_romopen) return l_real_romopen();
    return 1;
}

EXPORT void CALL RomClosed(void)
{
    if (l_stream) fflush(l_stream);
    if (l_chaining && l_real_romclosed) l_real_romclosed();
}

/* Keyboard events reach the plugin through these, so a keyboard profile is
 * dead without forwarding them. */
EXPORT void CALL SDL_KeyDown(int keymod, int keysym)
{
    if (l_chaining && l_real_keydown) l_real_keydown(keymod, keysym);
}

EXPORT void CALL SDL_KeyUp(int keymod, int keysym)
{
    if (l_chaining && l_real_keyup) l_real_keyup(keymod, keysym);
}
