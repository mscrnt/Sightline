/**
 * inputtest - the keyboard/mouse translation, asserted headless.
 *
 * WHY THIS EXISTS. The mirrored-mouse and dead-menu-navigation failures are
 * pure translation arithmetic:
 *
 *     key / dx / dy  ->  read_keyboard, read_mouse  ->  map_kbm
 *                    ->  the four movement channels, or the N64 stick
 *
 * None of that needs a window, a compositor or a real pointer, and every
 * attempt to drive it with a real one has produced contradictory readings:
 * SDL's own pointer warp fights xdotool, and SDL_PushEvent does not reach
 * SDL_GetRelativeMouseState at all. So this links the REAL src/platform/
 * sl_input.c - not a copy of its logic - against stub definitions of the
 * seventeen SDL entry points it uses, and drives those stubs directly.
 *
 * NOTHING IS ADDED TO PRODUCTION SOURCE for this. sl_input.c is compiled
 * unmodified; the substitution happens at the link, by not linking SDL. That
 * is deliberate: a test hook inside the shipping input path is a behaviour
 * difference waiting to be forgotten about, and this needs none.
 *
 * Build and run: tools/native/inputtest.sh
 */
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/platform/sl_input.h"
#include "../../src/platform/sl_action.h"
#include "../../src/platform/sl_bindings.h"
#include "../../src/platform/sl_bindings_editor.h"
#include "../../src/platform/sl_settings.h"
#include "../../src/sl_asset_override.h"   /* SL_PART_* for the visual snapshot (#63) */

/* --------------------------------------------------------------------------
 * the stub devices - what the real sl_input.c reads instead of SDL
 * ----------------------------------------------------------------------- */
static Uint8    stub_keys[SDL_NUM_SCANCODES];
static int      stub_dx, stub_dy;
static Uint32   stub_buttons;
static int      stub_relmode, stub_wingrab, stub_cursor = 1;

const Uint8 *SDL_GetKeyboardState(int *numkeys)
{
    if (numkeys) *numkeys = SDL_NUM_SCANCODES;
    return stub_keys;
}

Uint32 SDL_GetRelativeMouseState(int *x, int *y)
{
    if (x) *x = stub_dx;
    if (y) *y = stub_dy;
    stub_dx = stub_dy = 0;             /* draining, exactly as SDL does */
    return stub_buttons;
}

int  SDL_SetRelativeMouseMode(SDL_bool on) { stub_relmode = (on != SDL_FALSE); return 0; }
SDL_bool SDL_GetRelativeMouseMode(void) { return stub_relmode ? SDL_TRUE : SDL_FALSE; }
/* The MOUSE-SPECIFIC grab, which is what set_grab now calls. The legacy
 * SDL_SetWindowGrab is no longer referenced anywhere in the input layer. */
void SDL_SetWindowMouseGrab(SDL_Window *w, SDL_bool on) { (void) w; stub_wingrab = (on != SDL_FALSE); }
SDL_bool SDL_GetWindowMouseGrab(SDL_Window *w) { (void) w; return stub_wingrab ? SDL_TRUE : SDL_FALSE; }

/* The confinement rectangle. The harness models it as a stored rect so the
 * arm/disarm ORDER can be asserted: the barrier must be cleared before the
 * grab is released, or a real desktop pointer could be stranded inside our
 * window. */
static SDL_Rect stub_rect;
static int      stub_rect_set;
int SDL_SetWindowMouseRect(SDL_Window *w, const SDL_Rect *r)
{
    (void) w;
    if (r == NULL) { stub_rect_set = 0; return 0; }
    stub_rect = *r; stub_rect_set = 1; return 0;
}
const SDL_Rect *SDL_GetWindowMouseRect(SDL_Window *w)
{ (void) w; return stub_rect_set ? &stub_rect : NULL; }
void SDL_GetWindowSize(SDL_Window *w, int *x, int *y)
{ (void) w; if (x) *x = 960; if (y) *y = 720; }

/* THE ABSOLUTE POINTER. SDL_GetMouseState reports the position the event pump
 * last recorded, which is what read_pointer samples; SDL_GetMouseFocus reports
 * which window the pointer is over, and read_pointer refuses to sample unless
 * it is ours. Both are stubbed as plain stored state so the test can move the
 * pointer without a compositor - the same reasoning as the relative stubs
 * above, and for the same measured reason: SDL_PushEvent does not reach these
 * getters either. */
static int stub_px, stub_py, stub_mousefocus = 1;
static SDL_Window *stub_win;
Uint32 SDL_GetMouseState(int *x, int *y)
{ if (x) *x = stub_px; if (y) *y = stub_py; return stub_buttons; }
SDL_Window *SDL_GetMouseFocus(void)
{ return stub_mousefocus ? stub_win : NULL; }
const char *SDL_GetCurrentVideoDriver(void) { return "x11"; }
int  SDL_ShowCursor(int toggle) { if (toggle >= 0) stub_cursor = toggle; return stub_cursor; }
SDL_bool SDL_SetHint(const char *n, const char *v) { (void) n; (void) v; return SDL_TRUE; }
const char *SDL_GetError(void) { return "stub"; }
/* "x11" so read_env takes the same branch it takes on the owner's machine -
 * the one that turns on warp-based relative mode. SDL_SetHint is stubbed, so
 * this only exercises the branch, it does not change any behaviour asserted
 * below. */


/* No gamepad in this harness BY DEFAULT: the pad path has its own
 * owner-validated behaviour and this test is about the keyboard and mouse;
 * read_pad exits at the first of these. The #46 controller section flips
 * stub_pad_present and drives ONE synthetic pad through the same entry
 * points (a dummy handle, a button array, a trigger pair) so the BUTTON MODE
 * ORIGINAL / CUSTOM split can be asserted without hardware. */
static int    stub_pad_present;
static Uint8  stub_padb[SDL_CONTROLLER_BUTTON_MAX];
static Sint16 stub_pada[SDL_CONTROLLER_AXIS_MAX];
static int    stub_pad_dummy;
/* The subsystem, once initialised, STAYS initialised whether or not a pad is
 * attached - as the real SDL does. It used to answer "not initialised" while
 * no pad was present, which made a removal poll return before the rescan
 * could close the pad, so a re-attached pad was never re-opened (and, since
 * #63, never re-classified). Init always succeeds; presence is what
 * SDL_NumJoysticks / GetAttached report. */
static int    stub_sdl_inited;
int  SDL_InitSubSystem(Uint32 flags) { (void) flags; stub_sdl_inited = 1; return 0; }
int  SDL_NumJoysticks(void) { return stub_pad_present ? 1 : 0; }
SDL_bool SDL_IsGameController(int i) { (void) i; return stub_pad_present ? SDL_TRUE : SDL_FALSE; }
SDL_GameController *SDL_GameControllerOpen(int i) { (void) i; return stub_pad_present ? (SDL_GameController *) &stub_pad_dummy : NULL; }
const char *SDL_GameControllerName(SDL_GameController *g) { (void) g; return stub_pad_present ? "stub pad" : "none"; }
SDL_bool SDL_GameControllerGetAttached(SDL_GameController *g) { (void) g; return stub_pad_present ? SDL_TRUE : SDL_FALSE; }
Sint16 SDL_GameControllerGetAxis(SDL_GameController *g, SDL_GameControllerAxis a)
{ (void) g; return (stub_pad_present && a >= 0 && a < SDL_CONTROLLER_AXIS_MAX) ? stub_pada[a] : 0; }
Uint8 SDL_GameControllerGetButton(SDL_GameController *g, SDL_GameControllerButton b)
{ (void) g; return (stub_pad_present && b >= 0 && b < SDL_CONTROLLER_BUTTON_MAX) ? stub_padb[b] : 0; }
void SDL_GameControllerClose(SDL_GameController *g) { (void) g; }
/* Added by the multi-pad rescan (sl_input.c pad_rescan / read_pad). With no
 * pad present the list is empty and these are never reached with a device;
 * they exist so the link succeeds. Without them this harness had rotted out
 * of buildability against the shipping file it is supposed to be testing,
 * which is the one failure mode a link-the-real-thing test has. */
SDL_JoystickID SDL_JoystickGetDeviceInstanceID(int i) { (void) i; return stub_pad_present ? 7 : -1; }
SDL_Joystick *SDL_GameControllerGetJoystick(SDL_GameController *g) { (void) g; return stub_pad_present ? (SDL_Joystick *) &stub_pad_dummy : NULL; }
SDL_JoystickID SDL_JoystickInstanceID(SDL_Joystick *j) { (void) j; return stub_pad_present ? 7 : -1; }
Uint32 SDL_WasInit(Uint32 flags) { return stub_sdl_inited ? flags : 0; }
/* #63: SDL's controller-type verdict, the ONE input to the family
 * classification. The stub answers whatever the case set; the default is
 * UNKNOWN, which the shipping file must classify GENERIC. */
static SDL_GameControllerType stub_pad_type = SDL_CONTROLLER_TYPE_UNKNOWN;
SDL_GameControllerType SDL_GameControllerGetType(SDL_GameController *g) { (void) g; return stub_pad_type; }
/* The SL_PAD_VIRTUAL developer seam's four calls: link stubs, never reached
 * here (the harness sets no SL_PAD_VIRTUAL). */
int SDL_JoystickAttachVirtualEx(const SDL_VirtualJoystickDesc *d) { (void) d; return -1; }
SDL_Joystick *SDL_JoystickOpen(int i) { (void) i; return NULL; }
int SDL_JoystickSetVirtualAxis(SDL_Joystick *j, int a, Sint16 v) { (void) j; (void) a; (void) v; return -1; }
int SDL_JoystickSetVirtualButton(SDL_Joystick *j, int b, Uint8 v) { (void) j; (void) b; (void) v; return -1; }

/* --------------------------------------------------------------------------
 * the stub game - the three read-only windows sl_input.c has into it
 * ----------------------------------------------------------------------- */
static int stub_menu, stub_aim, stub_style, stub_upright;
int sl_game_control_style(int p) { (void) p; return stub_style; }
int sl_game_menu_mode(void) { return stub_menu; }
int sl_game_aim_mode(int p) { (void) p; return stub_aim; }
int sl_game_look_upright(void) { return stub_upright; }
int sl_live_input_active(void) { return 1; }
/* The two front-end questions the pointer path asks. Separate flags because
 * they are separate questions in production too: the boot chain answers yes to
 * the second and no to the first. */
static int stub_ptrmenu, stub_clickadv;
int sl_game_pointer_menu_active(void) { return stub_ptrmenu; }
int sl_game_click_advance_active(void) { return stub_clickadv; }
/* The third (the widescreen cursor repair): "is the pointer over the menu's
 * image" - the safe rect - which a click into a CURSOR menu is gated on. 1
 * by default, the 4:3 state, where every pixel of the window is the image. */
static int stub_ptrover = 1;
int sl_game_pointer_over_menu(void) { return stub_ptrover; }
void sl_run_mark(unsigned n) { (void) n; }
unsigned sl_record_index(void) { return 0; }
/* Two link stubs the shipping file grew after fbcc3a48 (the F8 live mark and
 * the movement-sidecar replay predicate); without them this harness did not
 * link at all against the file it tests. No replay is ever active here. */
void sl_mark_request(void) { }
int  sl_move_replay_active(void) { return 0; }
/* The watch's pointer consumer (#40, src/native/sl_watch_pointer.c): the left
 * button's edges while the watch is up. Link stubs only; no watch is up here. */
int  sl_watch_pointer_click(void) { return 0; }
void sl_watch_pointer_release(void) { }
/* #45: the SL_POINTER_PROBE developer witness stamps its lines with the VI
 * counter, and Escape inside a SIGHTLINE sub-menu steps back one level
 * (src/native/sl_game_query.c). Link stubs: no frames run and no sub-menu is
 * open here, so Escape keeps its watch-close meaning. */
unsigned sl_frames_completed(void) { return 0; }
int  sl_game_watch_child_open(void) { return 0; }
void sl_game_watch_child_back(void) { }
/* The settings store (#41, src/platform/sl_settings.c) is linked REAL since
 * #46 - the binding registry reads its "bind." lines and the pad BUTTON MODE
 * through it - and stays INACTIVE (no init), so gets answer the defaults and
 * sets change memory only: nothing is written by this harness. */

static int ch_on, ch_walk, ch_strafe, ch_turn, ch_pitch;
void sl_move_channels_set(int a, int w, int s, int t, int p)
{ ch_on = a; ch_walk = w; ch_strafe = s; ch_turn = t; ch_pitch = p; }

/* The LINEAR mouse look channel. A LINK STUB and nothing more - the harness
 * captures it so the shipping sl_input.c resolves, and asserts nothing about
 * it here. Adding an assertion would be a re-baseline of the 14 known B-096
 * failures, which project rule 1 makes its own commit. */
static int ml_on; static float ml_yaw, ml_pitch;
void sl_mouse_look_set(int a, float y, float p)
{ ml_on = a; ml_yaw = y; ml_pitch = p; }

/* THE ACTION CHANNELS (#38, src/native/sl_action_channels.c) and the two game
 * queries the action layer adds. Captured, not modelled: the harness records
 * what sl_input.c published this poll - the level mask and the per-action
 * edge counts - and the "is adjustable scoped aiming active" answer is a stub
 * flag like the others. The witness string is a diagnostic and returns
 * nothing here. Bit order = enum sl_action (sl_action.h). */
static int stub_scoped;
int sl_game_scoped_zoom_active(int p) { (void) p; return stub_scoped; }
int sl_game_action_witness(char *out, int n) { if (out && n > 0) out[0] = '\0'; return 0; }
static int      act_on;
static unsigned act_held;
static int      act_pulse[16];
void sl_action_channels_set(int active, unsigned held)
{ act_on = active; act_held = held; }
void sl_action_channels_pulse(int action, int count)
{ if (action >= 0 && action < 16) act_pulse[action] += count; }
#define ACT_INTERACT 0
#define ACT_RELOAD   1
#define ACT_CROUCH   2
#define ACT_WPREV    3
#define ACT_WNEXT    4
#define ACT_ZOOMIN   5
#define ACT_ZOOMOUT  6
#define ACT_SPRINT   7
/* #46: the six actions that used to be hard-wired scancodes / buttons. */
#define ACT_MOVE_F   8
#define ACT_MOVE_B   9
#define ACT_STRAFE_L 10
#define ACT_STRAFE_R 11
#define ACT_FIRE     12
#define ACT_AIM      13
/* #47: the texture-set cycle. It has NO action channel on purpose (the
 * setting is the renderer's, not the game's) - sl_input.c calls the native
 * consumer directly, so the harness captures THAT instead of a pulse. */
#define ACT_TEXCYCLE 14
static int tex_cycles;
void sl_textures_cycle(void) { tex_cycles++; }
static void act_clear(void) { memset(act_pulse, 0, sizeof act_pulse); }

/* --------------------------------------------------------------------------
 * harness
 * ----------------------------------------------------------------------- */
#define SL_BTN_A     0x8000
#define SL_BTN_B     0x4000
#define SL_BTN_Z     0x2000
#define SL_BTN_START 0x1000
#define SL_BTN_DUP   0x0800
#define SL_BTN_DLEFT 0x0200
#define SL_BTN_R     0x0010

/* The scratch config the #46 persistence section names through SL_CONFIG;
 * the store is a once-only singleton, so it stays active from then on and
 * the file is removed again at the end. Never the player's config. */
static char g_scratch_ini[512];

static int fails, checks;

static unsigned short out_b;
static signed char    out_x, out_y;

static void poll(void)
{
    sl_input_live_poll();
    sl_input_live_get(&out_b, &out_x, &out_y);
}

static void reset(void)
{
    memset(stub_keys, 0, sizeof stub_keys);
    stub_dx = stub_dy = 0;
    stub_buttons = 0;
}

static void ck(int cond, const char *what, const char *detail)
{
    checks++;
    if (!cond) {
        fails++;
        printf("  FAIL  %-46s %s\n", what, detail);
    } else {
        printf("  ok    %-46s %s\n", what, detail);
    }
}

static char buf[256];
static const char *st(void)
{
    snprintf(buf, sizeof buf,
             "button=%04x stick=(%d,%d) ch=%d walk=%d strafe=%d turn=%d pitch=%d",
             out_b, out_x, out_y, ch_on, ch_walk, ch_strafe, ch_turn, ch_pitch);
    return buf;
}

/* One poll with a single key held, in the given game context. */
static void press(int scancode, int menu, int aim)
{
    reset();
    stub_menu = menu; stub_aim = aim;
    stub_keys[scancode] = 1;
    poll();
}

/* One poll with a mouse delta, in the given game context. */
static void motion(int dx, int dy, int menu, int aim)
{
    reset();
    stub_menu = menu; stub_aim = aim;
    stub_dx = dx; stub_dy = dy;
    poll();
}

/* INVERT MOUSE Y precedence (#46 owner replay, 2026-09-18): SL_MOUSE_INVERT
 * against the settings store, at read_env's first poll. Two cases, each its
 * OWN PROCESS (INPUTTEST_CASE=mouse-invert-store / mouse-invert-nostore, run
 * by inputtest.ps1 / inputtest.sh before the main run): the store is a
 * once-only singleton and read_env runs once, so neither can share the run
 * in main below. Measured in the game first: with the variable left in the
 * shell, a config holding 0 loaded, the first poll read minv=1, the watch's
 * OFF wrote nothing (the store already held 0) and the next launch was ON
 * again. The contract asserted:
 *   store ACTIVE   (a player session) - the config's seed stands, the env is
 *                  ignored, and the setter still writes the store on change;
 *   store INACTIVE (replay, headless, this harness) - the env seeds the
 *                  state as it always did, an explicit set still wins. */
static int case_mouse_invert(int store_active)
{
    static char env_cfg[600];
    char path[512];
    const char *tmpdir = getenv("TEMP");
    FILE *f;
    char text[2048]; size_t n = 0;

    if (tmpdir == NULL || tmpdir[0] == '\0') tmpdir = ".";
    snprintf(path, sizeof path, "%s\\sl_inputtest_minv.ini", tmpdir);
    remove(path);
    snprintf(env_cfg, sizeof env_cfg, "SL_CONFIG=%s", path);
    putenv(env_cfg);
    putenv("SL_MOUSE_INVERT=1");

    printf("\n== INVERT MOUSE Y: SL_MOUSE_INVERT=1 with the store %s ==\n",
           store_active ? "ACTIVE (a player session)" : "INACTIVE (replay / harness)");
    if (store_active) {
        f = fopen(path, "w");
        if (f) { fputs("version=1\nmouse_invert_y=0\n", f); fclose(f); }
        sl_settings_init();
        ck(sl_settings_active() && sl_settings_get(SL_SET_MOUSE_INVERT_Y) == 0,
           "the store loaded mouse_invert_y=0", path);
    }
    /* what sl_settings_start does before the first poll, in both cases */
    sl_mouse_invert_y_seed(sl_settings_get(SL_SET_MOUSE_INVERT_Y));
    ck(sl_mouse_invert_y_get() == 0, "seeded 0 before the first poll", "");

    sl_input_live_set_window((void *) &checks);
    stub_win = (SDL_Window *) &checks;
    sl_input_live_focus(1);
    sl_input_live_click(SDL_BUTTON_LEFT, 1);
    stub_menu = 0; stub_aim = 0; stub_style = 0;
    poll();                                   /* read_env runs here, once */

    if (store_active) {
        ck(sl_mouse_invert_y_get() == 0,
           "first poll: the env does NOT displace the config (state stays 0)", "");
        sl_mouse_invert_y_set(1);
        f = fopen(path, "r"); n = 0;
        if (f) { n = fread(text, 1, sizeof text - 1, f); fclose(f); }
        text[n] = '\0';
        ck(sl_mouse_invert_y_get() == 1 && strstr(text, "mouse_invert_y=1\n") != NULL,
           "the menus' ON -> state 1 AND the file says 1", "");
        sl_mouse_invert_y_set(0);
        f = fopen(path, "r"); n = 0;
        if (f) { n = fread(text, 1, sizeof text - 1, f); fclose(f); }
        text[n] = '\0';
        ck(sl_mouse_invert_y_get() == 0 && strstr(text, "mouse_invert_y=0\n") != NULL,
           "the menus' OFF -> state 0 AND the file says 0 (store and state agree)", "");
        for (n = 0; n < 4; n++) poll();
        ck(sl_mouse_invert_y_get() == 0, "later polls never re-apply the env", "");
        /* The CONSUMER, at the one sign point: physical mouse UP (SDL dy < 0)
         * on the linear look channel is + (UP) with the state 0 and - (DOWN)
         * with it 1 - the same poll path the game's bondview2.c consumer
         * reads without inverting (#39). Relative to each other and to the
         * state, on this file's own Windows dy_sign default. */
        motion(0, -40, 0, 0);
        ck(ml_on && ml_pitch > 0.0f, "state 0: physical UP -> linear pitch + (UP)", "");
        sl_mouse_invert_y_set(1);
        motion(0, -40, 0, 0);
        ck(ml_on && ml_pitch < 0.0f, "state 1: physical UP -> linear pitch - (DOWN)", "");
        sl_mouse_invert_y_set(0);
        motion(0, -40, 0, 0);
        ck(ml_on && ml_pitch > 0.0f, "state 0 again: physical UP -> + (one sign point, no stacking)", "");
    } else {
        ck(!sl_settings_active(), "the store is inactive", "");
        ck(sl_mouse_invert_y_get() == 1,
           "first poll: the env seeds the state (no config governs)", "");
        sl_mouse_invert_y_set(0);
        ck(sl_mouse_invert_y_get() == 0, "an explicit set still wins over the env", "");
        f = fopen(path, "r");
        if (f) fclose(f);
        ck(f == NULL, "an inactive store writes no file", path);
    }
    remove(path);
    printf("\n%d checks, %d failed\n", checks, fails);
    return fails != 0;
}

/* MOUSE SENSITIVITY / SCOPED SENSITIVITY (#50): the two percents at the one
 * gameplay mouse-look seam, asserted on the linear channel sl_mouse_look_set
 * receives (degrees) and, where the number matters, the +/-70 fallback
 * channel. Its OWN PROCESS (INPUTTEST_CASE=mouse-sens) for the reason the
 * invert cases give: the store is a once-only singleton. The identity
 * witness is the number the pre-#50 binary produced for the same injected
 * delta - dx 40 at SL_MOUSE_SENS 6 -> 6.000000 degrees (0.15 deg/count),
 * captured at cc3a418a before the seam existed - so "100 / 100 changes
 * nothing" is asserted against a measurement, not against this build. */
static int case_mouse_sens(void)
{
    static char env_cfg[600];
    char path[512];
    const char *tmpdir = getenv("TEMP");
    FILE *f;
    char text[2048]; size_t n = 0;
    int px0, py0, px1, py1, ww, wh; unsigned m;
    signed char pad_x_100, pad_y_100, pad_x_50, pad_y_50;

    if (tmpdir == NULL || tmpdir[0] == '\0') tmpdir = ".";
    snprintf(path, sizeof path, "%s\\sl_inputtest_msens.ini", tmpdir);
    remove(path);
    snprintf(env_cfg, sizeof env_cfg, "SL_CONFIG=%s", path);
    putenv(env_cfg);

    printf("\n== MOUSE SENSITIVITY / SCOPED SENSITIVITY (#50): the gameplay look seam ==\n");
    sl_settings_init();
    ck(sl_settings_active() && sl_mouse_sens_get(0) == 100 && sl_mouse_sens_get(1) == 100,
       "a missing config: 100 / 100", path);

    sl_input_live_set_window((void *) &checks);
    stub_win = (SDL_Window *) &checks;
    sl_input_live_focus(1);
    sl_input_live_click(SDL_BUTTON_LEFT, 1);
    stub_menu = 0; stub_aim = 0; stub_style = 0; stub_scoped = 0;
    poll(); poll();                           /* capture; the changed-grab frame discarded */

    /* DEFAULT IDENTITY, against the pre-#50 measurement. */
    motion(40, 0, 0, 0);
    ck(ml_on && ml_yaw == 6.0f && ml_pitch == 0.0f && ch_turn == 70,
       "100/100: dx 40 -> yaw 6.000000 deg, turn 70 (the cc3a418a number)", "");
    motion(0, -40, 0, 0);
    ck(ml_on && ml_pitch == 6.0f && ml_yaw == 0.0f && ch_pitch == -70,
       "100/100: physical up 40 -> pitch +6.000000 (one constant, both axes)", "");
    motion(1, 0, 0, 0);
    ck(ml_on && ml_yaw == 0.15f, "100/100: one count -> 0.15 deg", "");

    /* THE STEP API lands on the grid and clamps; the store follows. */
    sl_mouse_sens_step(0, -1);
    ck(sl_mouse_sens_get(0) == 90, "step -1 from 100 -> 90", "");
    sl_settings_set(SL_SET_MOUSE_SENSITIVITY, 105);
    sl_mouse_sens_step(0, +1);
    ck(sl_mouse_sens_get(0) == 110, "step +1 from a hand-edited 105 -> 110 (the grid)", "");
    sl_settings_set(SL_SET_MOUSE_SENSITIVITY, 10);
    sl_mouse_sens_step(0, -1);
    ck(sl_mouse_sens_get(0) == 10, "step -1 at 10 stays 10 (the floor; zero is never reachable)", "");
    sl_settings_set(SL_SET_MOUSE_SENSITIVITY, 300);
    sl_mouse_sens_step(0, +1);
    ck(sl_mouse_sens_get(0) == 300, "step +1 at 300 stays 300 (the ceiling)", "");
    f = fopen(path, "r"); n = 0;
    if (f) { n = fread(text, 1, sizeof text - 1, f); fclose(f); }
    text[n] = '\0';
    ck(strstr(text, "mouse_sensitivity=300\n") != NULL && strstr(text, "scoped_mouse_sensitivity=100\n") != NULL,
       "the step persisted through the store (the file says 300 / 100)", "");

    /* LOWER / HIGHER: proportional on both channels, both axes. */
    sl_settings_set(SL_SET_MOUSE_SENSITIVITY, 50);
    motion(40, 0, 0, 0);
    ck(ml_on && ml_yaw == 3.0f, "50: dx 40 -> yaw 3.0 (half)", "");
    motion(0, -40, 0, 0);
    ck(ml_on && ml_pitch == 3.0f, "50: up 40 -> pitch +3.0 (half)", "");
    motion(13, -7, 0, 0);
    ck(ml_on && ml_yaw > 0.97499f && ml_yaw < 0.97501f && ml_pitch > 0.52499f && ml_pitch < 0.52501f
       && ch_turn == 34 && ch_pitch == -18,
       "50: diagonal (13,-7) -> 0.975 / 0.525 deg, fallback 34 / -18", "");
    sl_settings_set(SL_SET_MOUSE_SENSITIVITY, 200);
    motion(40, 0, 0, 0);
    ck(ml_on && ml_yaw == 12.0f, "200: dx 40 -> yaw 12.0 (double)", "");
    motion(0, 40, 0, 0);
    ck(ml_on && ml_pitch == -12.0f, "200: down 40 -> pitch -12.0 (double)", "");
    sl_settings_set(SL_SET_MOUSE_SENSITIVITY, 100);
    motion(40, 0, 0, 0);
    ck(ml_on && ml_yaw == 6.0f, "back to 100: 6.0 again", "");

    /* SCOPED: the second percent applies under the game's adjustable-scope
     * predicate only. Plain aim mode without such a scope is NOT scoped. */
    sl_settings_set(SL_SET_SCOPED_MOUSE_SENSITIVITY, 50);
    motion(40, 0, 0, 0);
    ck(ml_on && ml_yaw == 6.0f, "scoped 50, play: dx 40 -> 6.0 (normal look unchanged)", "");
    motion(40, 0, 0, 1);                      /* aim held, no adjustable scope */
    ck(ml_on && ml_yaw == 6.0f, "scoped 50, plain AIM (no scope): 6.0 - aiming is not scoped", "");
    stub_scoped = 1;
    motion(40, 0, 0, 1);
    ck(ml_on && ml_yaw == 3.0f, "scoped 50, in the scope: dx 40 -> 3.0", "");
    motion(0, -40, 0, 1);
    ck(ml_on && ml_pitch == 3.0f, "scoped 50, in the scope: up 40 -> pitch +3.0", "");
    stub_scoped = 0;
    motion(40, 0, 0, 0);
    ck(ml_on && ml_yaw == 6.0f, "out of the scope: 6.0 again", "");
    sl_settings_set(SL_SET_MOUSE_SENSITIVITY, 200);
    motion(40, 0, 0, 0);
    stub_scoped = 1;
    { float play = ml_yaw; motion(40, 0, 0, 1);
      ck(play == 12.0f && ml_yaw == 6.0f, "base 200 / scoped 50: play 12.0, scope 6.0 (the base scales both)", ""); }
    stub_scoped = 0;
    sl_settings_set(SL_SET_MOUSE_SENSITIVITY, 100);
    sl_settings_set(SL_SET_SCOPED_MOUSE_SENSITIVITY, 100);

    /* THE WHEEL keeps its context from the same predicate. */
    act_clear(); stub_scoped = 1; sl_input_live_wheel(1); motion(0, 0, 0, 1);
    ck(act_pulse[ACT_ZOOMIN] == 1 && act_pulse[ACT_WPREV] == 0, "wheel up in the scope -> ZOOM IN (unchanged)", "");
    act_clear(); stub_scoped = 0; sl_input_live_wheel(1); motion(0, 0, 0, 0);
    ck(act_pulse[ACT_WPREV] == 1 && act_pulse[ACT_ZOOMIN] == 0, "wheel up in play -> WEAPON PREVIOUS (unchanged)", "");
    act_clear();

    /* INVERT MOUSE Y composes independently: the sign is its, the gain ours. */
    sl_settings_set(SL_SET_MOUSE_SENSITIVITY, 50);
    sl_mouse_invert_y_set(1);
    motion(0, -40, 0, 0);
    ck(ml_on && ml_pitch == -3.0f, "invert ON at 50: up 40 -> pitch -3.0 (sign flipped, half gain)", "");
    motion(40, 0, 0, 0);
    ck(ml_on && ml_yaw == 3.0f, "invert ON at 50: yaw untouched by the invert, half gain", "");
    sl_mouse_invert_y_set(0);
    motion(0, -40, 0, 0);
    ck(ml_on && ml_pitch == 3.0f, "invert OFF at 50: +3.0 again", "");
    f = fopen(path, "r"); n = 0;
    if (f) { n = fread(text, 1, sizeof text - 1, f); fclose(f); }
    text[n] = '\0';
    ck(strstr(text, "mouse_invert_y=0\n") != NULL && strstr(text, "mouse_sensitivity=50\n") != NULL,
       "the file holds invert 0 beside sensitivity 50", "");

    /* THE WATCH POINTER (#40) does not read the gain: captured, the deltas are
     * the pointer, one window pixel per count at every percent. */
    stub_menu = 1; poll(); poll();
    sl_input_pointer_get(&px0, &py0, &ww, &wh, &m);
    stub_dx = 20; stub_dy = 10; poll();
    sl_input_pointer_get(&px1, &py1, &ww, &wh, &m);
    ck(px1 - px0 == 20 && py1 - py0 == 10 && !ml_on,
       "watch pointer at 50: (20,10) counts -> (20,10) pixels, no look published", "");
    sl_settings_set(SL_SET_MOUSE_SENSITIVITY, 300);
    sl_settings_set(SL_SET_SCOPED_MOUSE_SENSITIVITY, 300);
    stub_scoped = 1;
    sl_input_pointer_get(&px0, &py0, &ww, &wh, &m);
    stub_dx = -20; stub_dy = -10; poll();
    sl_input_pointer_get(&px1, &py1, &ww, &wh, &m);
    ck(px1 - px0 == -20 && py1 - py0 == -10 && !ml_on,
       "watch pointer at 300/300: (-20,-10) counts -> (-20,-10) pixels", "");
    stub_scoped = 0; stub_menu = 0; poll();

    /* THE CONTROLLER STICKS never see either percent (they are the walk /
     * strafe channels since #63, and those carry no mouse gain). */
    stub_pad_present = 1;
    sl_input_live_device_change();
    memset(stub_padb, 0, sizeof stub_padb); memset(stub_pada, 0, sizeof stub_pada);
    reset(); stub_menu = 0; poll();
    sl_settings_set(SL_SET_MOUSE_SENSITIVITY, 100);
    sl_settings_set(SL_SET_SCOPED_MOUSE_SENSITIVITY, 100);
    stub_pada[SDL_CONTROLLER_AXIS_LEFTX] = 20000; stub_pada[SDL_CONTROLLER_AXIS_LEFTY] = -24000; poll(); poll();
    pad_x_100 = ch_strafe; pad_y_100 = ch_walk;
    sl_settings_set(SL_SET_MOUSE_SENSITIVITY, 50);
    sl_settings_set(SL_SET_SCOPED_MOUSE_SENSITIVITY, 50);
    poll(); poll();
    pad_x_50 = ch_strafe; pad_y_50 = ch_walk;
    ck(ch_on == 1 && pad_x_100 != 0 && pad_y_100 != 0 && pad_x_50 == pad_x_100 && pad_y_50 == pad_y_100 && !ml_on,
       "a pad stick deflection is the same walk / strafe channel at 100/100 and 50/50, no linear look", st());
    stub_pada[SDL_CONTROLLER_AXIS_LEFTX] = 0; stub_pada[SDL_CONTROLLER_AXIS_LEFTY] = 0; poll();
    stub_pad_present = 0;
    sl_input_live_device_change();
    reset(); poll();

    remove(path);
    printf("\n%d checks, %d failed\n", checks, fails);
    return fails != 0;
}

/* THE MODERN PAD (#63): the synthetic pad through the one mapping that is
 * left, in its OWN PROCESS (INPUTTEST_CASE=modern-pad), store active on a
 * scratch config, for the reason the other cases give. What is asserted:
 *   - the family from SDL's controller type (Xbox / PlayStation / generic /
 *     none) and the labels the editors print for each - the token never
 *     moving;
 *   - the right stick as the continuous turn / pitch channels at 10 / 50 /
 *     100 percent, both axes, diagonal, a sweep round the gate; the C-BUTTON
 *     NEGATIVE CONTROL (no C bit on any deflection); the left stick as the
 *     continuous walk / strafe channels; aiming withholds walking;
 *   - the STICK LAYOUTS: each of the four routes the right axes to the right
 *     channels, SL_LOOK_INVERT-free here and the game's own Look Up/Down
 *     untouched (it is applied game-side, whichever stick carries pitch);
 *   - the BUTTON LAYOUTS: the DEFAULT preset IS the compiled table; each
 *     preset seeds the registry's PAD rows exactly (the layout_source table
 *     against the live table, and one behavioural probe per preset); editing
 *     a pad slot flips the row to CUSTOM; choosing the preset again re-seeds;
 *     RESET DEFAULTS reads DEFAULT; a recorded preset the lines contradict
 *     reads CUSTOM at load;
 *   - the LABEL RESOLVER: every action a control is bound to, from the live
 *     table, for both families' names;
 *   - the menu mapping; persistence (the file holds the two layout keys and
 *     none of the retired ones); no pad; the visual snapshot.
 * The raw values: pad_axis() takes the 5000-unit deadzone off and rescales,
 * so 10 / 50 / 100 percent of the remaining 27767 are raw 7777 / 18884 /
 * 32767 and land on channel 7 / 35 / 70. */
#define SL_BTN_CMASK  0x000F
#define SL_BTN_CDOWN  0x0004
#define SL_BTN_CRIGHT 0x0001
static void pad_axes(int lx, int ly, int rx, int ry)
{
    stub_pada[SDL_CONTROLLER_AXIS_LEFTX]  = (Sint16) lx;
    stub_pada[SDL_CONTROLLER_AXIS_LEFTY]  = (Sint16) ly;
    stub_pada[SDL_CONTROLLER_AXIS_RIGHTX] = (Sint16) rx;
    stub_pada[SDL_CONTROLLER_AXIS_RIGHTY] = (Sint16) ry;
}
static void pad_attach(SDL_GameControllerType type)
{
    stub_pad_present = 0; sl_input_live_device_change(); reset(); poll();
    stub_pad_type = type;
    stub_pad_present = 1; sl_input_live_device_change();
    memset(stub_padb, 0, sizeof stub_padb); memset(stub_pada, 0, sizeof stub_pada);
    reset(); poll(); act_clear();
}
/* Does the live PAD table equal a preset's rows, slot for slot? */
static int table_is_layout(int layout)
{
    int a, s;
    for (a = 0; a < SL_ACT_COUNT; a++)
        for (s = 0; s < SL_BIND_SLOTS; s++)
            if (!sl_bindings_source_equal(sl_bindings_get(a, 1, s), sl_bindings_layout_source(layout, a, s)))
                return 0;
    return 1;
}
/* One button held for a poll: which actions came up held? */
static unsigned held_after_button(SDL_GameControllerButton b)
{
    unsigned h;
    act_clear(); stub_padb[b] = 1; poll(); h = act_held;
    stub_padb[b] = 0; poll(); act_clear();
    return h;
}
static unsigned held_after_trigger(SDL_GameControllerAxis a)
{
    unsigned h;
    act_clear(); stub_pada[a] = 20000; poll(); h = act_held;
    stub_pada[a] = 0; poll(); act_clear();
    return h;
}
static int case_modern_pad(void)
{
    static char env_cfg[600];
    char path[512], t[32], l[32];
    const char *tmpdir = getenv("TEMP");
    FILE *f;
    char text[4096]; size_t n = 0;
    sl_bind_source s;
    sl_pad_visual v;
    int i, monotonic, acts[4], from;

    if (tmpdir == NULL || tmpdir[0] == '\0') tmpdir = ".";
    snprintf(path, sizeof path, "%s\\sl_inputtest_modern.ini", tmpdir);
    remove(path);
    snprintf(env_cfg, sizeof env_cfg, "SL_CONFIG=%s", path);
    putenv(env_cfg);

    printf("\n== THE MODERN PAD (#63): sticks, layouts, presets, labels on the synthetic pad ==\n");
    sl_settings_init();
    sl_bindings_reload();
    ck(sl_settings_active() && sl_bindings_layout_get() == SL_BUTTON_LAYOUT_DEFAULT
       && sl_settings_get(SL_SET_PAD_STICK_LAYOUT) == SL_STICK_LAYOUT_DEFAULT,
       "a missing config: BUTTON LAYOUT DEFAULT, STICK LAYOUT DEFAULT", path);
    ck(table_is_layout(SL_BUTTON_LAYOUT_DEFAULT), "the compiled pad table IS the DEFAULT preset", "");

    sl_input_live_set_window((void *) &checks);
    stub_win = (SDL_Window *) &checks;
    sl_input_live_focus(1);
    stub_menu = 0; stub_aim = 0; stub_style = 0; stub_scoped = 0;
    poll();

    /* ---- family ---------------------------------------------------- */
    ck(sl_input_pad_family() == SL_PAD_FAMILY_NONE && !sl_input_pad_visual(&v),
       "no pad: family NONE, no visual", sl_input_pad_family_name(sl_input_pad_family()));
    pad_attach(SDL_CONTROLLER_TYPE_XBOXONE);
    ck(sl_input_pad_family() == SL_PAD_FAMILY_XBOX, "SDL type XBOXONE -> family XBOX", "");
    pad_attach(SDL_CONTROLLER_TYPE_XBOX360);
    ck(sl_input_pad_family() == SL_PAD_FAMILY_XBOX, "SDL type XBOX360 -> family XBOX", "");
    pad_attach(SDL_CONTROLLER_TYPE_PS5);
    ck(sl_input_pad_family() == SL_PAD_FAMILY_PLAYSTATION, "SDL type PS5 -> family PLAYSTATION", "");
    pad_attach(SDL_CONTROLLER_TYPE_PS4);
    ck(sl_input_pad_family() == SL_PAD_FAMILY_PLAYSTATION, "SDL type PS4 -> family PLAYSTATION", "");
    pad_attach(SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO);
    ck(sl_input_pad_family() == SL_PAD_FAMILY_GENERIC, "SDL type SWITCH PRO -> family GENERIC", "");
    pad_attach(SDL_CONTROLLER_TYPE_UNKNOWN);
    ck(sl_input_pad_family() == SL_PAD_FAMILY_GENERIC, "SDL type UNKNOWN -> family GENERIC", "");

    /* ---- labels: token fixed, display by family ---------------------- */
    sl_bindings_source_parse("pad:A", &s);
    ck(!strcmp(sl_bindings_source_label(&s, SL_PAD_FAMILY_XBOX, l, 32), "A")
       && !strcmp(sl_bindings_source_token(&s, t, 32), "pad:A"),
       "pad:A labels A for XBOX, token pad:A", l);
    ck(!strcmp(sl_bindings_source_label(&s, SL_PAD_FAMILY_PLAYSTATION, l, 32), "CROSS")
       && !strcmp(sl_bindings_source_token(&s, t, 32), "pad:A"),
       "pad:A labels CROSS for PLAYSTATION, token pad:A", l);
    ck(!strcmp(sl_bindings_source_label(&s, SL_PAD_FAMILY_GENERIC, l, 32), "PAD A")
       && !strcmp(sl_bindings_source_label(&s, SL_PAD_FAMILY_NONE, l, 32), "PAD A"),
       "pad:A labels PAD A for GENERIC and NONE", l);
    sl_bindings_source_parse("pad:RT", &s);
    ck(!strcmp(sl_bindings_source_label(&s, SL_PAD_FAMILY_XBOX, l, 32), "RT")
       && !strcmp(sl_bindings_source_label(&s, SL_PAD_FAMILY_PLAYSTATION, t, 32), "R2"),
       "pad:RT labels RT / R2", l);
    sl_bindings_source_parse("pad:LS", &s);
    ck(!strcmp(sl_bindings_source_label(&s, SL_PAD_FAMILY_XBOX, l, 32), "LS")
       && !strcmp(sl_bindings_source_label(&s, SL_PAD_FAMILY_PLAYSTATION, t, 32), "L3"),
       "pad:LS labels LS / L3", l);
    sl_bindings_source_parse("pad:Y", &s);
    ck(!strcmp(sl_bindings_source_label(&s, SL_PAD_FAMILY_PLAYSTATION, l, 32), "TRIANGLE")
       && !strcmp(sl_bindings_source_label(&s, SL_PAD_FAMILY_XBOX, t, 32), "Y"),
       "pad:Y labels TRIANGLE / Y", l);
    sl_bindings_source_parse("key:F", &s);
    ck(!strcmp(sl_bindings_source_label(&s, SL_PAD_FAMILY_PLAYSTATION, l, 32), "F"),
       "a keyboard source ignores the family", l);
    pad_attach(SDL_CONTROLLER_TYPE_PS5);
    sl_bindings_source_parse("pad:B", &s);
    ck(!strcmp(sl_bindings_source_name(&s, l, 32), "CIRCLE"),
       "sl_bindings_source_name follows the ATTACHED family (PS5 -> CIRCLE)", l);
    pad_attach(SDL_CONTROLLER_TYPE_XBOXONE);
    ck(!strcmp(sl_bindings_source_name(&s, l, 32), "B"),
       "... and XBOX -> B, the same token", l);

    /* ---- the right stick: continuous look, never a C bit --------------- */
    pad_axes(0, 0, 7777, 0); poll();
    ck(ch_on == 1 && ch_turn == 7 && ch_pitch == 0 && ch_walk == 0 && ch_strafe == 0
       && out_x == 0 && out_y == 0 && (out_b & SL_BTN_CMASK) == 0 && !ml_on,
       "right X 10% -> turn 7, N64 stick neutral, no C bit, no mouse look", st());
    pad_axes(0, 0, 18884, 0); poll();
    ck(ch_on == 1 && ch_turn == 35 && (out_b & SL_BTN_CMASK) == 0, "right X 50% -> turn 35", st());
    pad_axes(0, 0, 32767, 0); poll();
    ck(ch_on == 1 && ch_turn == 70 && (out_b & SL_BTN_CMASK) == 0,
       "right X 100% -> turn 70 (the full native channel)", st());
    pad_axes(0, 0, -32767, 0); poll();
    ck(ch_turn == -70 && (out_b & SL_BTN_CMASK) == 0, "right X full left -> turn -70", st());
    pad_axes(0, 0, 0, -7777); poll();
    ck(ch_on == 1 && ch_pitch == -7 && ch_turn == 0 && (out_b & SL_BTN_CMASK) == 0,
       "right Y 10% up -> pitch -7 (channel + = down), no C bit", st());
    pad_axes(0, 0, 0, -18884); poll();
    ck(ch_pitch == -35, "right Y 50% up -> pitch -35", st());
    pad_axes(0, 0, 0, -32767); poll();
    ck(ch_pitch == -70 && (out_b & SL_BTN_CMASK) == 0, "right Y 100% up -> pitch -70", st());
    pad_axes(0, 0, 0, 32767); poll();
    ck(ch_pitch == 70 && (out_b & SL_BTN_CMASK) == 0, "right Y 100% down -> pitch +70", st());
    pad_axes(0, 0, 18884, -18884); poll();
    ck(ch_turn == 35 && ch_pitch == -35 && (out_b & SL_BTN_CMASK) == 0,
       "diagonal 50/50 -> turn 35 AND pitch -35 together", st());
    /* A sweep round the gate: twelve points on the full circle, every one
     * a smooth vector - turn follows cos, pitch follows -sin, no C bit. */
    monotonic = 1;
    {
        /* cos / sin of 0, 30, ... 330 degrees, in raw units (up = -y). */
        static const int cs[12][2] = {
            { 32767, 0 }, { 28377, -16383 }, { 16383, -28377 }, { 0, -32767 },
            { -16383, -28377 }, { -28377, -16383 }, { -32767, 0 },
            { -28377, 16383 }, { -16383, 28377 }, { 0, 32767 },
            { 16383, 28377 }, { 28377, 16383 }
        };
        int prev_turn = 0;
        for (i = 0; i < 12; i++) {
            pad_axes(0, 0, cs[i][0], cs[i][1]); poll();
            if ((out_b & SL_BTN_CMASK) != 0 || ch_on != 1) monotonic = 0;
            if (cs[i][0] > 6000 && ch_turn <= 0) monotonic = 0;
            if (cs[i][0] < -6000 && ch_turn >= 0) monotonic = 0;
            if (cs[i][1] < -6000 && ch_pitch >= 0) monotonic = 0;
            if (cs[i][1] > 6000 && ch_pitch <= 0) monotonic = 0;
            /* a 30-degree step moves turn by at most 35 (cos 60 -> cos 90) */
            if (i > 0 && (ch_turn - prev_turn > 40 || ch_turn - prev_turn < -40)) monotonic = 0;
            prev_turn = ch_turn;
        }
    }
    ck(monotonic, "twelve points round the gate - a smooth vector, never a C bit", st());
    pad_axes(0, 0, 0, 0); poll();
    ck(ch_turn == 0 && ch_pitch == 0, "right stick released -> 0 / 0", st());
    /* Aim mode: look stays live, walking withheld - the mouse's contract. */
    stub_aim = 1;
    pad_axes(32767, 0, 18884, 0); poll();
    ck(ch_on == 1 && ch_turn == 35 && ch_strafe == 0 && ch_walk == 0 && out_x == 0,
       "aiming: right stick turns (35), left stick withheld, N64 stick neutral", st());
    stub_aim = 0;
    pad_axes(0, 0, 0, 0); poll();

    /* ---- the left stick --------------------------------------------- */
    pad_axes(32767, 0, 0, 0); poll();
    ck(ch_on == 1 && ch_strafe == 70 && ch_walk == 0 && out_x == 0 && out_y == 0,
       "left X full -> strafe 70, N64 stick neutral", st());
    pad_axes(18884, 0, 0, 0); poll();
    ck(ch_strafe == 35, "left X 50% -> strafe 35 (proportional, never a W/A/S/D step)", st());
    pad_axes(0, -32767, 0, 0); poll();
    ck(ch_walk == 70 && ch_strafe == 0, "left Y full up -> walk 70", st());
    pad_axes(0, 7777, 0, 0); poll();
    ck(ch_walk == -7, "left Y 10% down -> walk -7", st());
    pad_axes(-18884, -18884, 0, 0); poll();
    ck(ch_walk == 35 && ch_strafe == -35, "left diagonal -> walk 35, strafe -35", st());
    pad_axes(0, 0, 0, 0); poll();

    /* ---- THE STICK LAYOUTS: one probe per axis per layout --------------- */
    printf("\n== STICK LAYOUTS (#63): which stick carries which channel pair ==\n");
    /* The probe: left X 50% right, left Y 100% up, right X 10% right, right Y
     * 100% down - four distinct magnitudes, so every channel names its axis. */
#define PROBE() do { pad_axes(18884, -32767, 7777, 32767); poll(); } while (0)
    sl_settings_set(SL_SET_PAD_STICK_LAYOUT, SL_STICK_LAYOUT_DEFAULT);
    PROBE();
    ck(ch_walk == 70 && ch_strafe == 35 && ch_turn == 7 && ch_pitch == 70,
       "DEFAULT: left = walk 70 / strafe 35, right = turn 7 / pitch +70", st());
    sl_settings_set(SL_SET_PAD_STICK_LAYOUT, SL_STICK_LAYOUT_SOUTHPAW);
    PROBE();
    ck(ch_walk == -70 && ch_strafe == 7 && ch_turn == 35 && ch_pitch == -70,
       "SOUTHPAW: right = walk -70 / strafe 7, left = turn 35 / pitch -70 (up)", st());
    sl_settings_set(SL_SET_PAD_STICK_LAYOUT, SL_STICK_LAYOUT_LEGACY);
    PROBE();
    ck(ch_walk == 70 && ch_turn == 35 && ch_pitch == 70 && ch_strafe == 7,
       "LEGACY: left = walk 70 / turn 35, right = pitch +70 / strafe 7", st());
    sl_settings_set(SL_SET_PAD_STICK_LAYOUT, SL_STICK_LAYOUT_LEGACY_SOUTHPAW);
    PROBE();
    ck(ch_walk == -70 && ch_turn == 7 && ch_pitch == -70 && ch_strafe == 35,
       "LEGACY SOUTHPAW: right = walk -70 / turn 7, left = pitch -70 / strafe 35", st());
    ck((out_b & SL_BTN_CMASK) == 0 && out_x == 0 && out_y == 0 && !ml_on,
       "under every layout: no C bit, the N64 stick neutral, no mouse look", st());
    /* A layout change is live on the next poll, no re-attach, and it is in
     * the file. */
    sl_settings_set(SL_SET_PAD_STICK_LAYOUT, SL_STICK_LAYOUT_DEFAULT);
    PROBE();
    ck(ch_walk == 70 && ch_turn == 7, "back to DEFAULT on the next poll", st());
    /* Aiming under SOUTHPAW: the look pair (now the left stick) stays live,
     * the move pair (the right stick) is withheld. */
    sl_settings_set(SL_SET_PAD_STICK_LAYOUT, SL_STICK_LAYOUT_SOUTHPAW);
    stub_aim = 1; PROBE();
    ck(ch_turn == 35 && ch_pitch == -70 && ch_walk == 0 && ch_strafe == 0,
       "SOUTHPAW aiming: the left stick still looks, the right stick is withheld", st());
    stub_aim = 0;
    sl_settings_set(SL_SET_PAD_STICK_LAYOUT, SL_STICK_LAYOUT_DEFAULT);
    pad_axes(0, 0, 0, 0); poll();
    /* Menus: the LEFT stick is the menu stick under every layout. */
    sl_settings_set(SL_SET_PAD_STICK_LAYOUT, SL_STICK_LAYOUT_SOUTHPAW);
    stub_menu = 2; poll();
    pad_axes(0, -32767, 0, 0); poll();
    ck(out_y == 80 && ch_on == 0, "SOUTHPAW menu: the LEFT stick still navigates (+80)", st());
    pad_axes(0, 0, 0, 32767); poll();
    ck(out_y == 0, "... and the right stick does not", st());
    pad_axes(0, 0, 0, 0); poll(); stub_menu = 0; poll();
    sl_settings_set(SL_SET_PAD_STICK_LAYOUT, SL_STICK_LAYOUT_DEFAULT);
#undef PROBE

    /* ---- the buttons through the DEFAULT preset ------------------------ */
    printf("\n== BUTTON LAYOUTS (#63): the DEFAULT preset on the buttons ==\n");
    ck(held_after_button(SDL_CONTROLLER_BUTTON_A) == (1u << ACT_INTERACT), "DEFAULT: A -> INTERACT", "");
    act_clear(); stub_padb[SDL_CONTROLLER_BUTTON_A] = 1; poll();
    ck((out_b & (SL_BTN_A | SL_BTN_B)) == 0, "... and no N64 A / B in play", st());
    stub_padb[SDL_CONTROLLER_BUTTON_A] = 0; poll(); act_clear();
    ck(held_after_button(SDL_CONTROLLER_BUTTON_X) == (1u << ACT_RELOAD), "DEFAULT: X -> RELOAD", "");
    ck(held_after_button(SDL_CONTROLLER_BUTTON_B) == (1u << ACT_CROUCH), "DEFAULT: B -> CROUCH", "");
    ck(held_after_button(SDL_CONTROLLER_BUTTON_Y) == 0u, "DEFAULT: Y unbound (the page says UNBOUND; what it should do is the owner's call, #64)", "");
    ck(held_after_button(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) == (1u << ACT_FIRE), "DEFAULT: RB -> FIRE (2026-09-21: the bumpers mirror the triggers)", "");
    ck(held_after_button(SDL_CONTROLLER_BUTTON_LEFTSHOULDER) == (1u << ACT_AIM), "DEFAULT: LB -> AIM", "");
    ck(held_after_button(SDL_CONTROLLER_BUTTON_LEFTSTICK) == (1u << ACT_SPRINT), "DEFAULT: L3 -> SPRINT", "");
    ck(held_after_button(SDL_CONTROLLER_BUTTON_RIGHTSTICK) == 0u, "DEFAULT: R3 unbound", "");
    /* THE D-PAD IN PLAY (round 6): four registry sources, no N64 d-pad bit. */
    ck(held_after_button(SDL_CONTROLLER_BUTTON_DPAD_UP) == (1u << ACT_ZOOMIN), "DEFAULT: d-pad up -> ZOOM IN", "");
    ck(held_after_button(SDL_CONTROLLER_BUTTON_DPAD_DOWN) == (1u << ACT_ZOOMOUT), "DEFAULT: d-pad down -> ZOOM OUT", "");
    ck(held_after_button(SDL_CONTROLLER_BUTTON_DPAD_LEFT) == (1u << ACT_WPREV), "DEFAULT: d-pad left -> PREVIOUS WEAPON", "");
    ck(held_after_button(SDL_CONTROLLER_BUTTON_DPAD_RIGHT) == (1u << ACT_WNEXT), "DEFAULT: d-pad right -> NEXT WEAPON", "");
    act_clear(); stub_padb[SDL_CONTROLLER_BUTTON_DPAD_UP] = 1; poll();
    ck((out_b & SL_BTN_DUP) == 0 && act_pulse[ACT_ZOOMIN] == 1 && (act_held & (1u << ACT_ZOOMIN)),
       "d-pad up in play: ZOOM IN edge + held, NO N64 D-UP (it would be C-up under Honey)", st());
    poll();
    ck(act_pulse[ACT_ZOOMIN] == 1 && (act_held & (1u << ACT_ZOOMIN)), "d-pad up held: still ONE edge, the level stays (a held zoom)", st());
    stub_padb[SDL_CONTROLLER_BUTTON_DPAD_UP] = 0; poll(); act_clear();
    stub_padb[SDL_CONTROLLER_BUTTON_DPAD_LEFT] = 1; poll();
    ck(sl_input_pad_visual(&v) && (v.held & (1u << SL_PART_DPAD_LEFT)) && (out_b & SL_BTN_DLEFT) == 0,
       "d-pad left held: the snapshot flags DPAD_LEFT (the page lights the d-pad), no N64 bit", st());
    stub_padb[SDL_CONTROLLER_BUTTON_DPAD_LEFT] = 0; poll(); act_clear();

    /* THE BUMPERS (owner, 2026-09-21): "I also don't like RB and LB and L1
     * and R1 cycling the weapons. They should just mirror the triggers for
     * aim and shoot. Dpad handles weapons fine." So in every live preset a
     * bumper is a second FIRE / AIM, and FIRE and AIM are UNPAIRED actions
     * whose rows carry no context - the bumper therefore acts in every
     * context, scoped or not. The scope-aware MECHANISM is untouched and is
     * exercised further down against a hand binding. */
    printf("\n== THE BUMPERS (2026-09-21): they mirror the triggers - FIRE and AIM, in every context ==\n");
    stub_scoped = 0;
    act_clear(); stub_padb[SDL_CONTROLLER_BUTTON_RIGHTSHOULDER] = 1; poll();
    ck((act_held & (1u << ACT_FIRE)) && (out_b & SL_BTN_Z)
       && (act_held & (1u << ACT_WNEXT)) == 0 && (act_held & (1u << ACT_ZOOMIN)) == 0,
       "RB unscoped: FIRE (Z), no weapon cycle and no zoom", st());
    stub_scoped = 1; poll();
    ck((act_held & (1u << ACT_FIRE)) && (out_b & SL_BTN_Z) && (act_held & (1u << ACT_ZOOMIN)) == 0,
       "RB scoped: STILL FIRE - an unpaired action has no context", st());
    stub_padb[SDL_CONTROLLER_BUTTON_RIGHTSHOULDER] = 0; stub_scoped = 0; poll(); act_clear();
    stub_padb[SDL_CONTROLLER_BUTTON_LEFTSHOULDER] = 1; poll();
    ck((act_held & (1u << ACT_AIM)) && (out_b & SL_BTN_R)
       && (act_held & (1u << ACT_WPREV)) == 0 && (act_held & (1u << ACT_ZOOMOUT)) == 0,
       "LB unscoped: AIM (R), no weapon cycle and no zoom", st());
    stub_scoped = 1; poll();
    ck((act_held & (1u << ACT_AIM)) && (out_b & SL_BTN_R) && (act_held & (1u << ACT_ZOOMOUT)) == 0,
       "LB scoped: STILL AIM", st());
    stub_padb[SDL_CONTROLLER_BUTTON_LEFTSHOULDER] = 0; poll(); act_clear();
    /* The zoom is the d-pad's alone now, in the scope as on foot. */
    ck(held_after_button(SDL_CONTROLLER_BUTTON_DPAD_UP) == (1u << ACT_ZOOMIN)
       && held_after_button(SDL_CONTROLLER_BUTTON_DPAD_DOWN) == (1u << ACT_ZOOMOUT),
       "scoped: the zoom is the d-pad's - up ZOOM IN, down ZOOM OUT", "");
    act_clear(); stub_padb[SDL_CONTROLLER_BUTTON_DPAD_UP] = 1; poll();
    ck(act_pulse[ACT_ZOOMIN] == 1 && (act_held & (1u << ACT_ZOOMIN)), "scoped: d-pad up one edge + the held level (the held zoom is unchanged)", st());
    poll();
    ck(act_pulse[ACT_ZOOMIN] == 1 && (act_held & (1u << ACT_ZOOMIN)), "scoped: d-pad up HELD - still one edge, the level stays", st());
    stub_padb[SDL_CONTROLLER_BUTTON_DPAD_UP] = 0; poll(); act_clear();
    /* The d-pad's zoom rows are plain levels: up is ZOOM IN in either
     * context (the game's seam applies it only in the scope), right is NEXT
     * WEAPON in either. */
    ck(held_after_button(SDL_CONTROLLER_BUTTON_DPAD_UP) == (1u << ACT_ZOOMIN)
       && held_after_button(SDL_CONTROLLER_BUTTON_DPAD_RIGHT) == (1u << ACT_WNEXT),
       "scoped: d-pad up still ZOOM IN, d-pad right still NEXT WEAPON (not contextual)", "");
    /* The wheel is unchanged by the rule's generalisation. */
    act_clear(); sl_input_live_wheel(1); poll();
    ck(act_pulse[ACT_ZOOMIN] == 1 && act_pulse[ACT_WPREV] == 0, "wheel up scoped -> ZOOM IN (unchanged)", st());
    stub_scoped = 0; poll(); act_clear();
    sl_input_live_wheel(1); poll();
    ck(act_pulse[ACT_WPREV] == 1 && act_pulse[ACT_ZOOMIN] == 0, "wheel up unscoped -> PREVIOUS WEAPON (unchanged)", st());
    act_clear(); sl_input_live_wheel(-2); poll();
    ck(act_pulse[ACT_WNEXT] == 2 && act_pulse[ACT_ZOOMOUT] == 0, "wheel down x2 unscoped -> NEXT WEAPON x2 (unchanged)", st());
    act_clear();
    /* Keys bound to the cycle keep today's behaviour: a level in either context. */
    stub_scoped = 1; act_clear(); press(SDL_SCANCODE_2, 0, 0);
    ck(act_pulse[ACT_WNEXT] == 1, "key 2 scoped -> NEXT WEAPON edge (keys are not contextual)", st());
    stub_scoped = 0; reset(); poll(); act_clear();
    /* THE SCOPE-AWARE MECHANISM IS UNCHANGED - no preset uses it on a
     * bumper any more, but the SOURCE flag still carries it, so a player
     * who binds the cycle and the zoom to one bumper by hand gets exactly
     * the round-6 behaviour. Built here as a CUSTOM binding, asserted, then
     * undone. */
    printf("\n== the scope-aware mechanism, on a HAND binding (the source flag is unchanged) ==\n");
    sl_bindings_source_parse("pad:RB", &s);
    sl_bindings_set(SL_ACT_WEAPON_NEXT, 1, 0, &s, &from);
    sl_bindings_set(SL_ACT_ZOOM_IN, 1, 0, &s, &from);
    ck(sl_bindings_layout_get() == SL_BUTTON_LAYOUT_CUSTOM
       && sl_bindings_source_ctx(sl_bindings_get(SL_ACT_WEAPON_NEXT, 1, 0), SL_ACT_WEAPON_NEXT) == SL_WHEEL_CTX_PLAY
       && sl_bindings_source_ctx(sl_bindings_get(SL_ACT_ZOOM_IN, 1, 0), SL_ACT_ZOOM_IN) == SL_WHEEL_CTX_SCOPED,
       "RB bound by hand to NEXT WEAPON and ZOOM IN: both rows coexist, one per context", "");
    stub_scoped = 0; act_clear(); stub_padb[SDL_CONTROLLER_BUTTON_RIGHTSHOULDER] = 1; poll();
    ck((act_held & (1u << ACT_WNEXT)) && act_pulse[ACT_WNEXT] == 1 && (act_held & (1u << ACT_ZOOMIN)) == 0,
       "the hand binding, unscoped: NEXT WEAPON edge, no ZOOM IN", st());
    act_clear();                      /* the press's own edge is counted; the stale rule is about what follows */
    stub_scoped = 1; poll();
    ck((act_held & (1u << ACT_ZOOMIN)) && act_pulse[ACT_WNEXT] == 0 && (act_held & (1u << ACT_WNEXT)) == 0,
       "the hand binding, scope raised with RB still down: ZOOM IN held, no stale cycle edge", st());
    stub_padb[SDL_CONTROLLER_BUTTON_RIGHTSHOULDER] = 0; stub_scoped = 0; poll(); act_clear();
    /* And the conflict policy is still the round-6 one: a third, contextless
     * use of RB steals BOTH rows. */
    sl_bindings_set(SL_ACT_RELOAD, 1, 1, &s, &from);
    ck(from >= 0 && sl_bindings_get(SL_ACT_WEAPON_NEXT, 1, 0)->kind == SL_SRC_NONE
       && sl_bindings_get(SL_ACT_ZOOM_IN, 1, 0)->kind == SL_SRC_NONE,
       "RB onto RELOAD (no context) steals it from NEXT WEAPON AND ZOOM IN", "");
    sl_bindings_layout_apply(SL_BUTTON_LAYOUT_DEFAULT);
    /* The live table's own contexts: a bumper on FIRE / AIM carries none,
     * and the d-pad's rows never did. */
    ck(sl_bindings_source_ctx(sl_bindings_get(SL_ACT_FIRE, 1, 1), SL_ACT_FIRE) == SL_CTX_NONE
       && sl_bindings_source_ctx(sl_bindings_get(SL_ACT_AIM, 1, 1), SL_ACT_AIM) == SL_CTX_NONE
       && sl_bindings_source_ctx(sl_bindings_get(SL_ACT_ZOOM_IN, 1, 0), SL_ACT_ZOOM_IN) == SL_CTX_NONE
       && sl_bindings_source_ctx(sl_bindings_get(SL_ACT_FIRE, 1, 0), SL_ACT_FIRE) == SL_CTX_NONE,
       "row contexts now: RB/FIRE none, LB/AIM none, d-pad up/ZOOM IN none, RT/FIRE none", "");
    ck(!strcmp(sl_bindings_pair_brief(SL_ACT_WEAPON_PREVIOUS, SL_ACT_WEAPON_NEXT), "WEAPONS")
       && !strcmp(sl_bindings_pair_brief(SL_ACT_ZOOM_OUT, SL_ACT_ZOOM_IN), "ZOOM")
       && sl_bindings_pair_brief(SL_ACT_ZOOM_IN, SL_ACT_WEAPON_NEXT) == NULL
       && sl_bindings_pair_brief(SL_ACT_FIRE, SL_ACT_AIM) == NULL,
       "pair briefs: WEAPONS, ZOOM, none across pairs or for unpaired actions", "");
    sl_bindings_source_parse("pad:DPAD_LEFT", &s);
    ck(!strcmp(sl_bindings_source_token(&s, t, 32), "pad:DPAD_LEFT")
       && !strcmp(sl_bindings_source_label(&s, SL_PAD_FAMILY_XBOX, l, 32), "D-PAD LEFT")
       && !strcmp(sl_bindings_source_label(&s, SL_PAD_FAMILY_PLAYSTATION, l, 32), "D-PAD LEFT")
       && !strcmp(sl_bindings_source_label(&s, SL_PAD_FAMILY_NONE, l, 32), "PAD D-PAD LEFT"),
       "pad:DPAD_LEFT: token round-trips, D-PAD LEFT on both families, PAD D-PAD LEFT neutral", l);
    act_clear(); stub_pada[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] = 20000; poll();
    ck((act_held & (1u << ACT_FIRE)) && (out_b & SL_BTN_Z), "DEFAULT: RT -> FIRE -> Z", st());
    stub_pada[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] = 7000; poll();
    ck((act_held & (1u << ACT_FIRE)) == 0 && (out_b & SL_BTN_Z) == 0,
       "RT below the 8000 threshold -> nothing (threshold kept)", st());
    stub_pada[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] = 0; poll(); act_clear();
    stub_pada[SDL_CONTROLLER_AXIS_TRIGGERLEFT] = 20000; poll();
    ck((act_held & (1u << ACT_AIM)) && (out_b & SL_BTN_R), "DEFAULT: LT -> AIM -> R", st());
    stub_pada[SDL_CONTROLLER_AXIS_TRIGGERLEFT] = 0; poll(); act_clear();
    stub_padb[SDL_CONTROLLER_BUTTON_START] = 1; poll();
    ck((out_b & SL_BTN_START) != 0, "Start -> START (the pause)", st());
    stub_padb[SDL_CONTROLLER_BUTTON_START] = 0;
    stub_menu = 1; poll();
    stub_padb[SDL_CONTROLLER_BUTTON_DPAD_UP] = 1; poll();
    ck((out_b & SL_BTN_DUP) != 0 && act_on == 0, "d-pad up in the WATCH -> N64 D-UP (the menus' navigation, round 6 unchanged)", st());
    stub_padb[SDL_CONTROLLER_BUTTON_DPAD_UP] = 0; poll(); stub_menu = 0; poll(); act_clear();

    /* ---- the presets: seeding, CUSTOM, re-seed, reset, load ------------ */
    printf("\n== BUTTON LAYOUTS (#63): seeding, CUSTOM, re-seed, reset, load ==\n");
    ck(!strcmp(sl_bindings_layout_name(SL_BUTTON_LAYOUT_DEFAULT), "DEFAULT")
       && !strcmp(sl_bindings_layout_name(SL_BUTTON_LAYOUT_SOUTHPAW), "SOUTHPAW")
       && !strcmp(sl_bindings_layout_name(SL_BUTTON_LAYOUT_BUMPER), "BUMPER")
       && !strcmp(sl_bindings_layout_name(SL_BUTTON_LAYOUT_GREEN_THUMB), "GREEN THUMB")
       && !strcmp(sl_bindings_layout_name(SL_BUTTON_LAYOUT_CUSTOM), "CUSTOM")
       && !strcmp(sl_bindings_layout_name(SL_BUTTON_LAYOUT_COUNT), "?"),
       "the five names, and ? out of range", "");
    ck(sl_bindings_layout_apply(SL_BUTTON_LAYOUT_SOUTHPAW) == 1
       && sl_bindings_layout_get() == SL_BUTTON_LAYOUT_SOUTHPAW && table_is_layout(SL_BUTTON_LAYOUT_SOUTHPAW),
       "SOUTHPAW applied: the row says SOUTHPAW, every pad slot is the preset's", "");
    ck(held_after_trigger(SDL_CONTROLLER_AXIS_TRIGGERLEFT) == (1u << ACT_FIRE)
       && held_after_trigger(SDL_CONTROLLER_AXIS_TRIGGERRIGHT) == (1u << ACT_AIM)
       && held_after_button(SDL_CONTROLLER_BUTTON_LEFTSHOULDER) == (1u << ACT_FIRE)
       && held_after_button(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) == (1u << ACT_AIM)
       && held_after_button(SDL_CONTROLLER_BUTTON_DPAD_UP) == (1u << ACT_ZOOMIN)
       && held_after_button(SDL_CONTROLLER_BUTTON_DPAD_LEFT) == (1u << ACT_WPREV),
       "SOUTHPAW: LT / LB fire, RT / RB aim (the bumpers mirror the swapped triggers), d-pad as DEFAULT", "");
    stub_scoped = 1;
    ck(held_after_button(SDL_CONTROLLER_BUTTON_LEFTSHOULDER) == (1u << ACT_FIRE)
       && held_after_button(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) == (1u << ACT_AIM),
       "SOUTHPAW scoped: the bumpers still fire and aim - no zoom on a bumper", "");
    stub_scoped = 0;
    /* BUMPER is RETIRED (owner, 2026-09-21): its point was the triggers
     * cycling weapons. The id stays occupied - pad_button_layout is
     * persisted and its ids are append-only - but it is never offered,
     * never applied and never matched. */
    ck(sl_bindings_layout_selectable(SL_BUTTON_LAYOUT_DEFAULT)
       && sl_bindings_layout_selectable(SL_BUTTON_LAYOUT_SOUTHPAW)
       && sl_bindings_layout_selectable(SL_BUTTON_LAYOUT_GREEN_THUMB)
       && !sl_bindings_layout_selectable(SL_BUTTON_LAYOUT_BUMPER)
       && !sl_bindings_layout_selectable(SL_BUTTON_LAYOUT_CUSTOM)
       && !sl_bindings_layout_selectable(-1),
       "the three live presets are selectable; the retired BUMPER, CUSTOM and an out-of-range id are not", "");
    ck(sl_bindings_layout_apply(SL_BUTTON_LAYOUT_BUMPER) == 0
       && sl_bindings_layout_get() == SL_BUTTON_LAYOUT_SOUTHPAW && table_is_layout(SL_BUTTON_LAYOUT_SOUTHPAW),
       "a retired preset cannot be applied - the table and the row are untouched", "");
    /* The editors' stepper walks the live presets only, and wraps. */
    ck(sl_bindings_layout_step(SL_BUTTON_LAYOUT_DEFAULT, +1) == SL_BUTTON_LAYOUT_SOUTHPAW
       && sl_bindings_layout_step(SL_BUTTON_LAYOUT_SOUTHPAW, +1) == SL_BUTTON_LAYOUT_GREEN_THUMB
       && sl_bindings_layout_step(SL_BUTTON_LAYOUT_GREEN_THUMB, +1) == SL_BUTTON_LAYOUT_DEFAULT,
       "stepping up: DEFAULT -> SOUTHPAW -> GREEN THUMB -> DEFAULT (BUMPER skipped)", "");
    ck(sl_bindings_layout_step(SL_BUTTON_LAYOUT_DEFAULT, -1) == SL_BUTTON_LAYOUT_GREEN_THUMB
       && sl_bindings_layout_step(SL_BUTTON_LAYOUT_GREEN_THUMB, -1) == SL_BUTTON_LAYOUT_SOUTHPAW
       && sl_bindings_layout_step(SL_BUTTON_LAYOUT_SOUTHPAW, -1) == SL_BUTTON_LAYOUT_DEFAULT,
       "stepping down: the same order backwards, BUMPER skipped", "");
    ck(sl_bindings_layout_step(SL_BUTTON_LAYOUT_CUSTOM, +1) == SL_BUTTON_LAYOUT_DEFAULT
       && sl_bindings_layout_step(SL_BUTTON_LAYOUT_CUSTOM, -1) == SL_BUTTON_LAYOUT_GREEN_THUMB
       && sl_bindings_layout_step(SL_BUTTON_LAYOUT_BUMPER, +1) == SL_BUTTON_LAYOUT_DEFAULT
       && sl_bindings_layout_step(SL_BUTTON_LAYOUT_DEFAULT, 0) == SL_BUTTON_LAYOUT_DEFAULT,
       "a step off CUSTOM or off the retired id lands on the first / last live preset; dir 0 stays", "");
    ck(sl_bindings_layout_apply(SL_BUTTON_LAYOUT_GREEN_THUMB) == 1 && table_is_layout(SL_BUTTON_LAYOUT_GREEN_THUMB)
       && held_after_button(SDL_CONTROLLER_BUTTON_RIGHTSTICK) == (1u << ACT_AIM)
       && held_after_trigger(SDL_CONTROLLER_AXIS_TRIGGERLEFT) == (1u << ACT_AIM)
       && held_after_button(SDL_CONTROLLER_BUTTON_B) == (1u << ACT_CROUCH)
       && held_after_button(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) == (1u << ACT_FIRE)
       && held_after_trigger(SDL_CONTROLLER_AXIS_TRIGGERRIGHT) == (1u << ACT_FIRE)
       && held_after_button(SDL_CONTROLLER_BUTTON_LEFTSHOULDER) == 0u,
       "GREEN THUMB: R3 and LT aim, RT / RB fire, B crouches; LB unbound - two slots, and R3 takes AIM's second", "");
    ck(sl_bindings_get(SL_ACT_CROUCH, 1, 0)->code == SDL_CONTROLLER_BUTTON_B,
       "GREEN THUMB: CROUCH has its pad source back (round 6 closed the flag)", "");
    ck(sl_bindings_layout_apply(SL_BUTTON_LAYOUT_DEFAULT) == 1 && table_is_layout(SL_BUTTON_LAYOUT_DEFAULT)
       && sl_bindings_layout_get() == SL_BUTTON_LAYOUT_DEFAULT && sl_settings_ext_count() == 0,
       "DEFAULT applied again: the compiled table, no bind. line held", "");
    ck(sl_bindings_layout_apply(SL_BUTTON_LAYOUT_DEFAULT) == 0, "applying the layout in force changes nothing", "");
    ck(sl_bindings_layout_apply(SL_BUTTON_LAYOUT_CUSTOM) == 0 && sl_bindings_layout_apply(-1) == 0
       && sl_bindings_layout_get() == SL_BUTTON_LAYOUT_DEFAULT,
       "CUSTOM and an out-of-range id cannot be applied", "");
    /* A hand edit flips the row to CUSTOM; the KBM column never does. */
    sl_bindings_source_parse("pad:B", &s);
    sl_bindings_set(SL_ACT_FIRE, 1, 1, &s, &from);
    ck(sl_bindings_layout_get() == SL_BUTTON_LAYOUT_CUSTOM && from == SL_ACT_CROUCH,
       "FIRE's pad secondary = B: the row flips to CUSTOM (B taken from CROUCH)", "");
    ck(held_after_button(SDL_CONTROLLER_BUTTON_B) == (1u << ACT_FIRE), "... and B fires (custom pad binding honoured)", "");
    sl_bindings_source_parse("key:G", &s);
    sl_bindings_set(SL_ACT_INTERACT, 0, 1, &s, &from);
    ck(sl_bindings_layout_get() == SL_BUTTON_LAYOUT_CUSTOM, "a KBM edit leaves the row where it was (CUSTOM)", "");
    ck(sl_bindings_layout_apply(SL_BUTTON_LAYOUT_DEFAULT) == 1 && table_is_layout(SL_BUTTON_LAYOUT_DEFAULT)
       && sl_bindings_layout_get() == SL_BUTTON_LAYOUT_DEFAULT
       && !strcmp(sl_bindings_source_token(sl_bindings_get(SL_ACT_INTERACT, 0, 1), t, 32), "key:G"),
       "choosing DEFAULT again re-seeds the pad column and leaves the KBM edit alone", t);
    /* Undoing the edit by hand lands back on the preset it equals. FIRE's
     * pad secondary is RB in the table since 2026-09-21, so putting it back
     * is what restores DEFAULT (before that the slot was empty). */
    sl_bindings_source_parse("pad:B", &s);
    sl_bindings_set(SL_ACT_FIRE, 1, 1, &s, &from);
    sl_bindings_source_parse("pad:RB", &s);
    sl_bindings_set(SL_ACT_FIRE, 1, 1, &s, &from);
    sl_bindings_source_parse("pad:B", &s);
    sl_bindings_set(SL_ACT_CROUCH, 1, 0, &s, &from);
    ck(sl_bindings_layout_get() == SL_BUTTON_LAYOUT_DEFAULT && table_is_layout(SL_BUTTON_LAYOUT_DEFAULT),
       "hand-editing the table back to a preset reads as that preset again", "");
    sl_bindings_layout_apply(SL_BUTTON_LAYOUT_SOUTHPAW);
    sl_bindings_reset_defaults();
    ck(sl_bindings_layout_get() == SL_BUTTON_LAYOUT_DEFAULT && table_is_layout(SL_BUTTON_LAYOUT_DEFAULT)
       && sl_bindings_get(SL_ACT_INTERACT, 0, 1)->kind == SL_SRC_NONE,
       "RESET DEFAULTS: DEFAULT, the compiled table, the KBM edit gone too", "");
    /* Load: a recorded preset the lines contradict reads CUSTOM. */
    sl_settings_set(SL_SET_PAD_BUTTON_LAYOUT, SL_BUTTON_LAYOUT_SOUTHPAW);
    sl_bindings_reload();
    ck(sl_bindings_layout_get() == SL_BUTTON_LAYOUT_CUSTOM && table_is_layout(SL_BUTTON_LAYOUT_DEFAULT),
       "a stored SOUTHPAW with the DEFAULT table's lines loads as CUSTOM (the row never lies)", "");
    sl_bindings_layout_apply(SL_BUTTON_LAYOUT_SOUTHPAW);
    sl_bindings_reload();
    ck(sl_bindings_layout_get() == SL_BUTTON_LAYOUT_SOUTHPAW && table_is_layout(SL_BUTTON_LAYOUT_SOUTHPAW),
       "a stored SOUTHPAW with SOUTHPAW's lines reloads as SOUTHPAW", "");
    /* THE RETIRED ID AT LOAD, both ways round (owner, 2026-09-21). A config
     * that recorded BUMPER loads as CUSTOM whatever its lines say: with the
     * old preset's own lines it keeps them (nobody's controls change under
     * them), and with no pad lines at all it holds the new compiled
     * DEFAULT table - which is what a DEFAULT player gets with no manual
     * step, because their file records DEFAULT and has no pad lines. */
    sl_settings_set(SL_SET_PAD_BUTTON_LAYOUT, SL_BUTTON_LAYOUT_BUMPER);
    sl_bindings_reload();
    ck(sl_bindings_layout_get() == SL_BUTTON_LAYOUT_CUSTOM && table_is_layout(SL_BUTTON_LAYOUT_SOUTHPAW),
       "a stored BUMPER loads as CUSTOM and keeps the file's own bind lines", "");
    sl_bindings_reset_defaults();
    sl_settings_set(SL_SET_PAD_BUTTON_LAYOUT, SL_BUTTON_LAYOUT_BUMPER);
    sl_bindings_reload();
    ck(sl_bindings_layout_get() == SL_BUTTON_LAYOUT_CUSTOM && table_is_layout(SL_BUTTON_LAYOUT_DEFAULT)
       && held_after_button(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) == (1u << ACT_FIRE),
       "a stored BUMPER with NO pad lines: CUSTOM over the new compiled table (RB fires)", "");
    sl_bindings_layout_apply(SL_BUTTON_LAYOUT_DEFAULT);
    /* The DEFAULT player's upgrade, stated as its own check: the recorded
     * preset is DEFAULT, there are no pad bind lines, so the new table is
     * simply in force. */
    sl_settings_set(SL_SET_PAD_BUTTON_LAYOUT, SL_BUTTON_LAYOUT_DEFAULT);
    sl_bindings_reload();
    ck(sl_bindings_layout_get() == SL_BUTTON_LAYOUT_DEFAULT && table_is_layout(SL_BUTTON_LAYOUT_DEFAULT)
       && held_after_button(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) == (1u << ACT_FIRE)
       && held_after_button(SDL_CONTROLLER_BUTTON_LEFTSHOULDER) == (1u << ACT_AIM),
       "an existing DEFAULT config with no pad bind lines gets the new table: RB fires, LB aims", "");

    /* ---- THE LABEL RESOLVER ----------------------------------------- */
    printf("\n== THE LABEL RESOLVER (#63): actions per control, from the live table ==\n");
    sl_bindings_source_parse("pad:RT", &s);
    ck(sl_bindings_actions_for_source(&s, 1, acts, 4) == 1 && acts[0] == SL_ACT_FIRE
       && !strcmp(sl_bindings_action_label(acts[0]), "FIRE"),
       "pad:RT -> [FIRE]", "");
    sl_bindings_source_parse("pad:RB", &s);
    ck(sl_bindings_actions_for_source(&s, 1, acts, 4) == 1 && acts[0] == SL_ACT_FIRE
       && !strcmp(sl_bindings_action_label(acts[0]), "FIRE"),
       "pad:RB -> [FIRE] (2026-09-21: the page's row reads RB FIRE / R1 FIRE)", "");
    sl_bindings_source_parse("pad:DPAD_DOWN", &s);
    ck(sl_bindings_actions_for_source(&s, 1, acts, 4) == 1 && acts[0] == SL_ACT_ZOOM_OUT, "pad:DPAD_DOWN -> [ZOOM OUT]", "");
    sl_bindings_source_parse("pad:LS", &s);
    ck(sl_bindings_actions_for_source(&s, 1, acts, 4) == 1 && acts[0] == SL_ACT_SPRINT, "pad:LS -> [SPRINT]", "");
    sl_bindings_source_parse("pad:A", &s);
    sl_bindings_set(SL_ACT_FIRE, 1, 1, &s, &from);
    ck(from == SL_ACT_INTERACT && sl_bindings_actions_for_source(&s, 1, acts, 4) == 1 && acts[0] == SL_ACT_FIRE,
       "pad:A moved onto FIRE: INTERACT lost it (the steal), the resolver says [FIRE] only", "");
    ck(sl_bindings_actions_for_source(&s, 1, acts, 0) == 1,
       "... the count is the truth even with no room in the buffer", "");
    sl_bindings_source_parse("pad:LB", &s);
    ck(sl_bindings_actions_for_source(&s, 1, acts, 4) == 1 && acts[0] == SL_ACT_AIM
       && sl_bindings_get(SL_ACT_INTERACT, 1, 0)->kind == SL_SRC_NONE,
       "pad:LB -> [AIM]; INTERACT now has no pad source (its row reads UNBOUND)", "");
    sl_bindings_set(SL_ACT_RELOAD, 1, 0, NULL, &from);
    sl_bindings_source_parse("pad:X", &s);
    ck(sl_bindings_actions_for_source(&s, 1, acts, 4) == 0, "pad:X unbound -> nothing (the page's row reads UNBOUND)", "");
    ck(sl_bindings_actions_for_source(&s, 0, acts, 4) == 0, "a pad source has no KBM actions", "");
    sl_bindings_source_parse("key:E", &s);
    ck(sl_bindings_actions_for_source(&s, 0, acts, 4) == 1 && acts[0] == SL_ACT_INTERACT
       && sl_bindings_actions_for_source(&s, 1, acts, 4) == 0,
       "key:E -> [INTERACT] on the KBM column, nothing on the PAD column", "");
    ck(sl_bindings_actions_for_source(NULL, 1, acts, 4) == 0, "NULL -> 0", "");

    /* THE NORTH FACE BUTTON, the owner-observed omission (2026-09-20): the
     * watch's controller page dropped a control the resolver found no
     * action for, and pad:Y is bound by NONE of the four presets, so Xbox
     * Y / DualSense TRIANGLE was absent from the page on every one of them.
     * The page now prints the family's name with UNBOUND beside it; what
     * this harness can assert is the seam the page reads - the label from
     * the authoritative table, and the action count that decides the text -
     * for the four presets, for a hand binding and after a reset. No
     * binding for FACE_NORTH is invented anywhere: it is 0 in each preset
     * below, which is the point. */
    {
        int lay;
        sl_bindings_source_parse("pad:Y", &s);
        ck(!strcmp(sl_bindings_source_label(&s, SL_PAD_FAMILY_XBOX, l, 32), "Y")
           && !strcmp(sl_bindings_source_label(&s, SL_PAD_FAMILY_PLAYSTATION, t, 32), "TRIANGLE")
           && !strcmp(sl_bindings_source_token(&s, t, 32), "pad:Y"),
           "FACE_NORTH: Y on an Xbox pad, TRIANGLE on a DualSense, one token", "");
        for (lay = 0; lay < SL_BUTTON_LAYOUT_PRESETS; lay++) {
            sl_bindings_layout_apply(lay);
            sl_bindings_source_parse("pad:Y", &s);
            ck(sl_bindings_actions_for_source(&s, 1, acts, 4) == 0,
               "FACE_NORTH is unbound in every preset (the row reads UNBOUND)", sl_bindings_layout_name(lay));
        }
        sl_bindings_layout_apply(SL_BUTTON_LAYOUT_DEFAULT);
        /* Bound by hand: the row shows that action, with no special case. */
        sl_bindings_source_parse("pad:Y", &s);
        sl_bindings_set(SL_ACT_RELOAD, 1, 0, &s, &from);
        ck(sl_bindings_actions_for_source(&s, 1, acts, 4) == 1 && acts[0] == SL_ACT_RELOAD
           && !strcmp(sl_bindings_action_label(acts[0]), "RELOAD"),
           "FACE_NORTH bound to RELOAD -> [RELOAD] (the row shows the action)", "");
        ck(sl_bindings_layout_get() == SL_BUTTON_LAYOUT_CUSTOM,
           "... and binding it is an ordinary edit: the layout row reads CUSTOM", "");
        sl_bindings_reset_defaults();
        sl_bindings_source_parse("pad:Y", &s);
        ck(sl_bindings_actions_for_source(&s, 1, acts, 4) == 0
           && sl_bindings_layout_get() == SL_BUTTON_LAYOUT_DEFAULT,
           "RESET DEFAULTS puts FACE_NORTH back to unbound", "");
    }
    /* The two families name the same control differently, the action the same. */
    sl_bindings_source_parse("pad:RT", &s);
    ck(!strcmp(sl_bindings_source_label(&s, SL_PAD_FAMILY_XBOX, l, 32), "RT")
       && !strcmp(sl_bindings_source_label(&s, SL_PAD_FAMILY_PLAYSTATION, t, 32), "R2")
       && sl_bindings_actions_for_source(&s, 1, acts, 4) == 1 && acts[0] == SL_ACT_FIRE,
       "the page's line: RT FIRE on an Xbox pad, R2 FIRE on a DualSense", "");
    sl_bindings_reset_defaults(); poll(); act_clear();

    /* ---- the visual snapshot ------------------------------------------ */
    pad_axes(0, 0, 18884, 0); stub_padb[SDL_CONTROLLER_BUTTON_A] = 1;
    stub_pada[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] = 20000; poll();
    ck(sl_input_pad_visual(&v) && v.right_x > 0.49f && v.right_x < 0.51f && v.left_x == 0.0f
       && (v.held & (1u << SL_PART_FACE_SOUTH)) && (v.held & (1u << SL_PART_RIGHT_TRIGGER))
       && (v.held & (1u << SL_PART_FACE_EAST)) == 0 && v.right_trigger > 0.6f,
       "visual: right stick 0.5, FACE_SOUTH and RIGHT_TRIGGER held, trigger 0.61", "");
    pad_axes(0, 0, 0, 0); stub_padb[SDL_CONTROLLER_BUTTON_A] = 0;
    stub_pada[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] = 0; poll(); act_clear();
    ck(sl_input_pad_visual(&v) && v.held == 0u && v.right_x == 0.0f, "visual: released -> nothing held", "");
    /* The snapshot is PHYSICAL: SOUTHPAW does not move the sticks in it. */
    sl_settings_set(SL_SET_PAD_STICK_LAYOUT, SL_STICK_LAYOUT_SOUTHPAW);
    pad_axes(0, 0, 18884, 0); poll();
    ck(sl_input_pad_visual(&v) && v.right_x > 0.49f && v.left_x == 0.0f,
       "visual under SOUTHPAW: the right stick still reads as the right stick", "");
    pad_axes(0, 0, 0, 0); poll();
    sl_settings_set(SL_SET_PAD_STICK_LAYOUT, SL_STICK_LAYOUT_DEFAULT);

    /* ---- menus ------------------------------------------------------ */
    stub_menu = 2; poll();
    stub_padb[SDL_CONTROLLER_BUTTON_A] = 1; poll();
    ck((out_b & SL_BTN_A) && ch_on == 0 && act_on == 0, "menu: A -> N64 A (accept), channels off", st());
    stub_padb[SDL_CONTROLLER_BUTTON_A] = 0;
    stub_padb[SDL_CONTROLLER_BUTTON_B] = 1; poll();
    ck((out_b & SL_BTN_B) != 0, "menu: B -> N64 B (back)", st());
    stub_padb[SDL_CONTROLLER_BUTTON_B] = 0;
    pad_axes(0, -32767, 0, 0); poll();
    ck(out_y == 80 && ch_on == 0, "menu: left stick up -> the menu stick (+80)", st());
    pad_axes(0, 0, 0, 0); poll();
    stub_menu = 0; poll(); act_clear();

    /* ---- the keyboard still works ----------------------------------- */
    press(SDL_SCANCODE_W, 0, 0);
    ck(ch_on == 1 && ch_walk == 70, "W still walks 70 through the keyboard", st());
    reset(); poll();

    /* ---- persistence ----------------------------------------------- */
    sl_settings_set(SL_SET_PAD_STICK_LAYOUT, SL_STICK_LAYOUT_LEGACY);
    sl_bindings_layout_apply(SL_BUTTON_LAYOUT_SOUTHPAW);
    f = fopen(path, "r"); n = 0;
    if (f) { n = fread(text, 1, sizeof text - 1, f); fclose(f); }
    text[n] = '\0';
    ck(strstr(text, "pad_button_layout=1\n") != NULL && strstr(text, "pad_stick_layout=2\n") != NULL
       && strstr(text, "bind.fire.pad.1=pad:LT\n") != NULL
       && strstr(text, "controller_profile=") == NULL && strstr(text, "pad_button_mode=") == NULL
       && strstr(text, "control_style=") == NULL,
       "the file holds pad_button_layout=1, pad_stick_layout=2, SOUTHPAW's lines, none of the retired keys", "");
    sl_bindings_layout_apply(SL_BUTTON_LAYOUT_DEFAULT);
    sl_settings_set(SL_SET_PAD_STICK_LAYOUT, SL_STICK_LAYOUT_DEFAULT);

    /* ---- no pad: nothing breaks, the keyboard is the owner ------------- */
    stub_pad_present = 0; sl_input_live_device_change(); reset(); poll();
    ck(sl_input_pad_family() == SL_PAD_FAMILY_NONE && !sl_input_pad_visual(&v),
       "pad removed: family NONE, no visual, poll survives", "");
    press(SDL_SCANCODE_W, 0, 0);
    ck(ch_on == 1 && ch_walk == 70, "... and W walks 70", st());
    reset(); poll();

    remove(path);
    printf("\n%d checks, %d failed\n", checks, fails);
    return fails != 0;
}

/* CONTROLLER TUNING (#51, 2026-09-20): LOOK SENSITIVITY / LOOK DEADZONE /
 * MOVE DEADZONE on the synthetic pad, in its OWN PROCESS (INPUTTEST_CASE=
 * pad-tune; pad-tune-invert is the same with SL_LOOK_INVERT=1 in the
 * environment before the first poll), store active on a scratch config
 * written BEFORE init with a malformed line per key so the fallback is the
 * first thing asserted. What is pinned:
 *   - THE DEFAULT IDENTITY: at 100 / 15 / 15 every channel value below is
 *     the number the 04b92554 tree produced for the same raw axis - the
 *     scratch witness's table (the analog round's witness-pre.txt, 230
 *     lines, byte-identical to the post-edit run with no config) - on both
 *     sticks, both signs, the boundary raws 4999 / 5000 / 5001, the
 *     diagonals and the four layouts;
 *   - the tune accessors: get, the step grid (5 / 1), the clamps, the
 *     fraction and the set from one, the file lines;
 *   - sensitivity: 50 halves the look channel (35 at full stick), 200
 *     doubles it and clamps at 70 from half the remaining travel on, the
 *     sign kept, the move pair and the picture untouched;
 *   - deadzone: 30 percent (10000 raw) zeroes raw 10000 and wakes 10001,
 *     rescales the remainder (16384 -> 19), 0 wakes raw 5001 (-> 10), full
 *     stick 70 either way, the negative side mirrored; the move deadzone
 *     the same on its pair and not on the look pair;
 *   - the layouts: the look deadzone and the gain follow the look pair
 *     (SOUTHPAW's left stick, LEGACY's LX / RY, LEGACY SOUTHPAW's RX / LY),
 *     the move deadzone the other two, under each of the four;
 *   - the negative controls: the d-pad, the bumpers in and out of the
 *     scope, the mouse's look and its sensitivity, the keyboard's walk and
 *     the menu stick are what they were.
 * The raw arithmetic: dz raw = p x 5000 / 15; pad_axis = (raw - dz) /
 * (32767 - dz); channel = (int) (v x sens x 70). */
static int case_pad_tune(int invert)
{
    static char env_cfg[600];
    char path[512];
    const char *tmpdir = getenv("TEMP");
    FILE *f;
    char text[4096]; size_t n = 0;
    sl_pad_visual v;
    int i, ok, sgn = invert ? -1 : 1;
    /* The oracle rows (witness-pre.txt at 04b92554): raw -> channel on the
     * DEFAULT layout's right X (turn) - the same numbers for right Y
     * (pitch, + = SDL down), left X (strafe) and left Y (walk, negated). */
    static const struct { int raw, ch; } oracle[] = {
        { 0, 0 }, { 1, 0 }, { 100, 0 }, { 1000, 0 }, { 3277, 0 }, { 4999, 0 }, { 5000, 0 },
        { 5001, 0 }, { 5500, 1 }, { 7777, 7 }, { 8192, 8 }, { 10000, 12 }, { 12000, 17 },
        { 16384, 28 }, { 18884, 35 }, { 20000, 37 }, { 24576, 49 }, { 29490, 61 },
        { 32000, 68 }, { 32767, 70 }
    };
#define NORACLE ((int) (sizeof oracle / sizeof oracle[0]))

    if (tmpdir == NULL || tmpdir[0] == '\0') tmpdir = ".";
    snprintf(path, sizeof path, "%s\\sl_inputtest_padtune.ini", tmpdir);
    remove(path);
    f = fopen(path, "w");
    if (f) {
        fputs("version=1\npad_look_sensitivity=fast\npad_look_deadzone=1.5\npad_move_deadzone=\n"
              "mouse_sensitivity=50\n", f);
        fclose(f);
    }
    snprintf(env_cfg, sizeof env_cfg, "SL_CONFIG=%s", path);
    putenv(env_cfg);
    if (invert)
        putenv("SL_LOOK_INVERT=1");

    printf("\n== CONTROLLER TUNING (#51): look sensitivity, look / move deadzone on the synthetic pad%s ==\n",
           invert ? " (SL_LOOK_INVERT=1)" : "");
    sl_settings_init();
    sl_bindings_reload();
    ck(sl_settings_active() && sl_pad_tune_get(SL_PAD_TUNE_LOOK_SENS) == 100
       && sl_pad_tune_get(SL_PAD_TUNE_LOOK_DEADZONE) == 15 && sl_pad_tune_get(SL_PAD_TUNE_MOVE_DEADZONE) == 15
       && sl_settings_get(SL_SET_MOUSE_SENSITIVITY) == 50,
       "malformed pad_look_sensitivity / deadzones -> 100 / 15 / 15, mouse_sensitivity=50 read beside them", path);
    sl_settings_set(SL_SET_MOUSE_SENSITIVITY, 100);

    sl_input_live_set_window((void *) &checks);
    stub_win = (SDL_Window *) &checks;
    sl_input_live_focus(1);
    stub_menu = 0; stub_aim = 0; stub_style = 0; stub_scoped = 0;
    poll();
    pad_attach(SDL_CONTROLLER_TYPE_XBOXONE);

    /* ---- the default identity ---------------------------------------- */
    printf("\n== the default identity: 100 / 15 / 15 against the 04b92554 table ==\n");
    ok = 1;
    for (i = 0; i < NORACLE; i++) {
        pad_axes(0, 0, oracle[i].raw, 0); poll();
        if (ch_turn != oracle[i].ch || ch_pitch != 0) { ok = 0; printf("    RX+%d -> turn %d (oracle %d)\n", oracle[i].raw, ch_turn, oracle[i].ch); }
        pad_axes(0, 0, -oracle[i].raw, 0); poll();
        if (ch_turn != -oracle[i].ch) { ok = 0; printf("    RX-%d -> turn %d (oracle %d)\n", oracle[i].raw, ch_turn, -oracle[i].ch); }
        pad_axes(0, 0, 0, oracle[i].raw); poll();
        if (ch_pitch != sgn * oracle[i].ch || ch_turn != 0) { ok = 0; printf("    RY+%d -> pitch %d (oracle %d)\n", oracle[i].raw, ch_pitch, sgn * oracle[i].ch); }
        pad_axes(0, 0, 0, -oracle[i].raw); poll();
        if (ch_pitch != -sgn * oracle[i].ch) { ok = 0; printf("    RY-%d -> pitch %d (oracle %d)\n", oracle[i].raw, ch_pitch, -sgn * oracle[i].ch); }
        pad_axes(oracle[i].raw, 0, 0, 0); poll();
        if (ch_strafe != oracle[i].ch || ch_walk != 0) { ok = 0; printf("    LX+%d -> strafe %d (oracle %d)\n", oracle[i].raw, ch_strafe, oracle[i].ch); }
        pad_axes(0, -oracle[i].raw, 0, 0); poll();
        if (ch_walk != oracle[i].ch || ch_strafe != 0) { ok = 0; printf("    LY-%d -> walk %d (oracle %d)\n", oracle[i].raw, ch_walk, oracle[i].ch); }
        pad_axes(-oracle[i].raw, oracle[i].raw, 0, 0); poll();
        if (ch_walk != -oracle[i].ch || ch_strafe != -oracle[i].ch) { ok = 0; printf("    L(-%d,+%d) -> walk %d strafe %d (oracle %d)\n", oracle[i].raw, oracle[i].raw, ch_walk, ch_strafe, -oracle[i].ch); }
    }
    ck(ok, "default: every oracle raw on both sticks, both signs, matches the 04b92554 channel", "20 raws x 7 probes");
    pad_axes(0, 0, -32768, 32767); poll();
    ck(ch_turn == -70 && ch_pitch == sgn * 70, "default: raw -32768 / 32767 -> -70 / +70 (the ends)", st());
    pad_axes(0, 0, 16384, 16384); poll();
    ck(ch_turn == 28 && ch_pitch == sgn * 28, "default: right (50,50) -> turn 28, pitch 28 (the diagonal, per axis)", st());
    pad_axes(0, 0, 18884, -18884); poll();
    ck(ch_turn == 35 && ch_pitch == -sgn * 35, "default: right (r50,-r50) -> 35 / -35", st());
    pad_axes(0, 0, 5000, 5000); poll();
    ck(ch_turn == 0 && ch_pitch == 0 && sl_input_pad_visual(&v) && v.right_x == 0.0f && v.right_y == 0.0f,
       "default: (5000,5000) is dead on both axes, the picture neutral", st());
    pad_axes(0, 0, 5001, -5001); poll();
    ck(ch_turn == 0 && ch_pitch == 0 && sl_input_pad_visual(&v) && v.right_x > 0.0f && v.right_y < 0.0f,
       "default: (5001,-5001) is live (0.000036) but truncates to channel 0, as before", st());
    pad_axes(0, 0, 0, 0); poll();

    /* ---- the accessors ----------------------------------------------- */
    printf("\n== the tune accessors: grid, clamps, fraction, file ==\n");
    sl_pad_tune_step(SL_PAD_TUNE_LOOK_SENS, +1);
    ck(sl_pad_tune_get(SL_PAD_TUNE_LOOK_SENS) == 105, "LOOK SENSITIVITY + -> 105 (step 5)", "");
    sl_pad_tune_step(SL_PAD_TUNE_LOOK_SENS, -1); sl_pad_tune_step(SL_PAD_TUNE_LOOK_SENS, -1);
    ck(sl_pad_tune_get(SL_PAD_TUNE_LOOK_SENS) == 95, "- - -> 95", "");
    sl_settings_set(SL_SET_PAD_LOOK_SENSITIVITY, 103); sl_pad_tune_step(SL_PAD_TUNE_LOOK_SENS, +1);
    ck(sl_pad_tune_get(SL_PAD_TUNE_LOOK_SENS) == 105, "a hand-edited 103 steps to 105 (the grid), not 108", "");
    for (i = 0; i < 40; i++) sl_pad_tune_step(SL_PAD_TUNE_LOOK_SENS, +1);
    ck(sl_pad_tune_get(SL_PAD_TUNE_LOOK_SENS) == 200 && sl_pad_tune_fraction(SL_PAD_TUNE_LOOK_SENS) == 1.0f,
       "forty + -> 200 (the maximum), fraction 1.0", "");
    for (i = 0; i < 80; i++) sl_pad_tune_step(SL_PAD_TUNE_LOOK_SENS, -1);
    ck(sl_pad_tune_get(SL_PAD_TUNE_LOOK_SENS) == 25 && sl_pad_tune_fraction(SL_PAD_TUNE_LOOK_SENS) == 0.0f,
       "eighty - -> 25 (the minimum, never 0), fraction 0.0", "");
    sl_pad_tune_set_fraction(SL_PAD_TUNE_LOOK_SENS, 0.5f);
    ck(sl_pad_tune_get(SL_PAD_TUNE_LOOK_SENS) == 25 + 18 * 5, "a click at 0.5 -> 115 (18 of 35 grid points from 25)", "");
    sl_pad_tune_set_fraction(SL_PAD_TUNE_LOOK_SENS, 3.0f);
    ck(sl_pad_tune_get(SL_PAD_TUNE_LOOK_SENS) == 200, "a fraction past 1 clamps to 200", "");
    sl_pad_tune_set_fraction(SL_PAD_TUNE_LOOK_SENS, -1.0f);
    ck(sl_pad_tune_get(SL_PAD_TUNE_LOOK_SENS) == 25, "a fraction below 0 clamps to 25", "");
    sl_settings_set(SL_SET_PAD_LOOK_SENSITIVITY, 100);
    sl_pad_tune_step(SL_PAD_TUNE_LOOK_DEADZONE, +1);
    ck(sl_pad_tune_get(SL_PAD_TUNE_LOOK_DEADZONE) == 16, "LOOK DEADZONE + -> 16 (step 1)", "");
    for (i = 0; i < 30; i++) sl_pad_tune_step(SL_PAD_TUNE_LOOK_DEADZONE, -1);
    ck(sl_pad_tune_get(SL_PAD_TUNE_LOOK_DEADZONE) == 0 && sl_pad_tune_fraction(SL_PAD_TUNE_LOOK_DEADZONE) == 0.0f,
       "thirty - -> 0 (no deadzone is allowed), fraction 0.0", "");
    for (i = 0; i < 60; i++) sl_pad_tune_step(SL_PAD_TUNE_LOOK_DEADZONE, +1);
    ck(sl_pad_tune_get(SL_PAD_TUNE_LOOK_DEADZONE) == 40 && sl_pad_tune_fraction(SL_PAD_TUNE_LOOK_DEADZONE) == 1.0f,
       "sixty + -> 40 (the maximum), fraction 1.0", "");
    sl_pad_tune_set_fraction(SL_PAD_TUNE_LOOK_DEADZONE, 0.5f);
    ck(sl_pad_tune_get(SL_PAD_TUNE_LOOK_DEADZONE) == 20, "a click at 0.5 -> 20", "");
    sl_pad_tune_set_fraction(SL_PAD_TUNE_MOVE_DEADZONE, 0.25f);
    ck(sl_pad_tune_get(SL_PAD_TUNE_MOVE_DEADZONE) == 10 && sl_pad_tune_get(SL_PAD_TUNE_LOOK_DEADZONE) == 20,
       "MOVE DEADZONE at 0.25 -> 10, LOOK DEADZONE still 20 (separate rows)", "");
    ck(sl_pad_tune_get(-1) == 0 && sl_pad_tune_get(SL_PAD_TUNE_COUNT) == 0 && sl_pad_tune_fraction(9) == 0.0f,
       "an out-of-range row reads 0 and cannot be stepped", "");
    f = fopen(path, "r"); n = 0;
    if (f) { n = fread(text, 1, sizeof text - 1, f); fclose(f); }
    text[n] = '\0';
    ck(strstr(text, "pad_look_sensitivity=100\n") != NULL && strstr(text, "pad_look_deadzone=20\n") != NULL
       && strstr(text, "pad_move_deadzone=10\n") != NULL && strstr(text, "mouse_sensitivity=100\n") != NULL
       && strstr(text, "pad_stick_layout=0\n") != NULL,
       "the file holds pad_look_sensitivity=100, pad_look_deadzone=20, pad_move_deadzone=10 beside the mouse and layout keys", "");
    sl_settings_set(SL_SET_PAD_LOOK_DEADZONE, 15);
    sl_settings_set(SL_SET_PAD_MOVE_DEADZONE, 15);

    /* ---- LOOK SENSITIVITY -------------------------------------------- */
    printf("\n== LOOK SENSITIVITY: lower / default / higher, the sign, the clamp ==\n");
    sl_settings_set(SL_SET_PAD_LOOK_SENSITIVITY, 50);
    pad_axes(0, 0, 32767, 0); poll();
    ck(ch_turn == 35 && sl_input_pad_visual(&v) && v.right_x == 1.0f, "50%: full right X -> turn 35 (half), the picture still 1.0", st());
    pad_axes(0, 0, 16384, -16384); poll();
    ck(ch_turn == 14 && ch_pitch == -sgn * 14, "50%: (50,-50) -> 14 / -14 (28 x 0.5, the diagonal kept)", st());
    pad_axes(0, 0, -7777, 7777); poll();
    ck(ch_turn == -3 && ch_pitch == sgn * 3, "50%: -7777 / 7777 -> -3 / +3 (7 x 0.5 truncates, the sign kept)", st());
    pad_axes(0, 0, 5500, 0); poll();
    ck(ch_turn == 0, "50%: raw 5500 (1 at 100%) -> 0", st());
    pad_axes(32767, -32767, 0, 0); poll();
    ck(ch_strafe == 70 && ch_walk == 70 && ch_turn == 0, "50%: the move pair is NOT scaled (70 / 70)", st());
    sl_settings_set(SL_SET_PAD_LOOK_SENSITIVITY, 100);
    pad_axes(0, 0, 32767, 0); poll();
    ck(ch_turn == 70, "100%: full right X -> 70 again", st());
    sl_settings_set(SL_SET_PAD_LOOK_SENSITIVITY, 200);
    pad_axes(0, 0, 7777, 0); poll();
    ck(ch_turn == 14, "200%: raw 7777 (7 at 100%) -> 14", st());
    pad_axes(0, 0, 16384, 16384); poll();
    ck(ch_turn == 57 && ch_pitch == sgn * 57, "200%: (50,50) -> 57 / 57 (0.41 x 2 x 70)", st());
    pad_axes(0, 0, 18884, 0); poll();
    ck(ch_turn == 70, "200%: half the remaining travel -> 70 (the clamp, never past the game's full rate)", st());
    pad_axes(0, 0, -32767, 32767); poll();
    ck(ch_turn == -70 && ch_pitch == sgn * 70, "200%: full stick -> -70 / +70, stable, no overflow", st());
    pad_axes(0, 0, 5001, 0); poll();
    ck(ch_turn == 0, "200%: raw 5001 -> still 0 (the deadzone is before the gain)", st());
    sl_settings_set(SL_SET_PAD_LOOK_SENSITIVITY, 25);
    pad_axes(0, 0, 32767, 0); poll();
    ck(ch_turn == 17, "25%: full right X -> 17 (the floor is never 0)", st());
    sl_settings_set(SL_SET_PAD_LOOK_SENSITIVITY, 100);
    pad_axes(0, 0, 0, 0); poll();

    /* ---- LOOK DEADZONE ----------------------------------------------- */
    printf("\n== LOOK DEADZONE: below / at / above, both signs, larger kills, smaller wakes ==\n");
    sl_settings_set(SL_SET_PAD_LOOK_DEADZONE, 30);        /* 10000 raw */
    pad_axes(0, 0, 9999, 0); poll();  ok = ch_turn == 0;
    pad_axes(0, 0, 10000, 0); poll(); ok = ok && ch_turn == 0 && sl_input_pad_visual(&v) && v.right_x == 0.0f;
    pad_axes(0, 0, 10001, 0); poll(); ok = ok && ch_turn == 0 && sl_input_pad_visual(&v) && v.right_x > 0.0f;
    ck(ok, "30%: raw 9999 / 10000 dead (picture neutral), 10001 live (1 / 22767)", st());
    pad_axes(0, 0, 7777, 0); poll();
    ck(ch_turn == 0, "30%: raw 7777 (7 at 15%) -> 0 (a larger deadzone kills a previously live input)", st());
    pad_axes(0, 0, 16384, 0); poll();
    ck(ch_turn == 19, "30%: raw 16384 -> 19 ((16384 - 10000) / 22767 x 70, the remainder rescaled)", st());
    pad_axes(0, 0, -16384, 0); poll();
    ck(ch_turn == -19, "30%: raw -16384 -> -19 (mirrored)", st());
    pad_axes(0, 0, 0, -10001); poll(); ok = ch_pitch == 0 && sl_input_pad_visual(&v) && v.right_y < 0.0f;
    pad_axes(0, 0, 0, -10000); poll(); ok = ok && ch_pitch == 0 && sl_input_pad_visual(&v) && v.right_y == 0.0f;
    pad_axes(0, 0, 0, -16384); poll(); ok = ok && ch_pitch == -sgn * 19;
    ck(ok, "30%: right Y -10001 live / -10000 dead / -16384 -> -19", st());
    pad_axes(0, 0, 32767, -32768); poll();
    ck(ch_turn == 70 && ch_pitch == -sgn * 70, "30%: full stick -> 70 / -70 (max stable)", st());
    pad_axes(0, 0, 16384, 16384); poll();
    ck(ch_turn == 19 && ch_pitch == sgn * 19, "30%: (50,50) -> 19 / 19 (the direction kept: per-axis, no bias)", st());
    pad_axes(0, 0, 16384, 8000); poll();
    ck(ch_turn == 19 && ch_pitch == 0, "30%: (16384, 8000) -> 19 / 0 (the square zone: one axis dead does not kill the other)", st());
    pad_axes(16384, -7777, 0, 0); poll();
    ck(ch_strafe == 28 && ch_walk == 7, "30% look: the move pair keeps its 15% (28 / 7)", st());
    sl_settings_set(SL_SET_PAD_LOOK_DEADZONE, 0);
    pad_axes(0, 0, 5001, 0); poll();
    ck(ch_turn == 10, "0%: raw 5001 -> 10 (a smaller deadzone wakes a previously dead input: 5001 / 32767 x 70)", st());
    pad_axes(0, 0, 1, 0); poll(); ok = ch_turn == 0 && sl_input_pad_visual(&v) && v.right_x > 0.0f;
    pad_axes(0, 0, 0, 0); poll(); ok = ok && ch_turn == 0 && sl_input_pad_visual(&v) && v.right_x == 0.0f;
    ck(ok, "0%: raw 1 live (truncates to 0), raw 0 neutral", st());
    pad_axes(0, 0, 16384, -16384); poll();
    ck(ch_turn == 35 && ch_pitch == -sgn * 35, "0%: (50,-50) -> 35 / -35 (16384 / 32767 x 70)", st());
    pad_axes(0, 0, 32767, 0); poll();
    ck(ch_turn == 70, "0%: full stick -> 70", st());
    sl_settings_set(SL_SET_PAD_LOOK_DEADZONE, 40);       /* 13333 raw */
    pad_axes(0, 0, 13333, 0); poll(); ok = ch_turn == 0;
    pad_axes(0, 0, 13334, 0); poll(); ok = ok && ch_turn == 0 && sl_input_pad_visual(&v) && v.right_x > 0.0f;
    pad_axes(0, 0, 32767, 0); poll(); ok = ok && ch_turn == 70;
    ck(ok, "40%: 13333 dead, 13334 live, full stick still 70", st());
    sl_settings_set(SL_SET_PAD_LOOK_DEADZONE, 15);
    pad_axes(0, 0, 7777, 0); poll();
    ck(ch_turn == 7, "15% again: raw 7777 -> 7 (the default restored on the next poll)", st());
    pad_axes(0, 0, 0, 0); poll();

    /* ---- MOVE DEADZONE ----------------------------------------------- */
    printf("\n== MOVE DEADZONE: the move pair only ==\n");
    sl_settings_set(SL_SET_PAD_MOVE_DEADZONE, 30);
    pad_axes(10000, -10000, 0, 0); poll();
    ck(ch_strafe == 0 && ch_walk == 0 && sl_input_pad_visual(&v) && v.left_x == 0.0f, "30% move: raw 10000 dead on both move axes", st());
    pad_axes(16384, -16384, 0, 0); poll();
    ck(ch_strafe == 19 && ch_walk == 19, "30% move: raw 16384 -> 19 / 19", st());
    pad_axes(-32767, 32767, 0, 0); poll();
    ck(ch_strafe == -70 && ch_walk == -70, "30% move: full stick -> -70 / -70 (max deflection unchanged)", st());
    pad_axes(0, 0, 7777, -7777); poll();
    ck(ch_turn == 7 && ch_pitch == -sgn * 7, "30% move: the look pair keeps its 15% (7 / -7)", st());
    stub_menu = 2; poll();
    pad_axes(0, -32767, 0, 0); poll();
    ck(out_y == 80 && ch_on == 0, "30% move, in a menu: the left stick still navigates at full deflection (+80)", st());
    pad_axes(0, 0, 0, 0); poll(); stub_menu = 0; poll();
    sl_settings_set(SL_SET_PAD_MOVE_DEADZONE, 15);

    /* ---- the layouts ------------------------------------------------- */
    printf("\n== THE LAYOUTS: the look tuning follows the look pair, the move deadzone the other ==\n");
    sl_settings_set(SL_SET_PAD_LOOK_SENSITIVITY, 50);
    sl_settings_set(SL_SET_PAD_LOOK_DEADZONE, 30);
    sl_settings_set(SL_SET_PAD_MOVE_DEADZONE, 15);
    /* The probe: every axis at raw 16384 (right / down on SDL's y): under
     * 15% that is 28, under 30% + 50% it is 9 (0.2804 x 0.5 x 70 = 9.8). */
#define TPROBE() do { pad_axes(16384, 16384, 16384, 16384); poll(); } while (0)
    sl_settings_set(SL_SET_PAD_STICK_LAYOUT, SL_STICK_LAYOUT_DEFAULT);
    TPROBE();
    ck(ch_strafe == 28 && ch_walk == -28 && ch_turn == 9 && ch_pitch == sgn * 9,
       "DEFAULT: left = move 28 / -28 (15%), right = look 9 / 9 (30%, x0.5)", st());
    sl_settings_set(SL_SET_PAD_STICK_LAYOUT, SL_STICK_LAYOUT_SOUTHPAW);
    TPROBE();
    ck(ch_strafe == 28 && ch_walk == -28 && ch_turn == 9 && ch_pitch == sgn * 9,
       "SOUTHPAW: right = move 28 / -28 (15%), LEFT = look 9 / 9 (the left stick is the tuned one)", st());
    pad_axes(10000, -10000, 7777, 7777); poll();
    ck(ch_turn == 0 && ch_pitch == 0 && ch_strafe == 7 && ch_walk == -7,
       "SOUTHPAW: left 10000 dead (30% look), right 7777 live (15% move: 7 / -7)", st());
    sl_settings_set(SL_SET_PAD_STICK_LAYOUT, SL_STICK_LAYOUT_LEGACY);
    TPROBE();
    ck(ch_walk == -28 && ch_strafe == 28 && ch_turn == 9 && ch_pitch == sgn * 9,
       "LEGACY: LY walk -28 / RX strafe 28 (15%), LX turn 9 / RY pitch 9 (30%, x0.5)", st());
    sl_settings_set(SL_SET_PAD_STICK_LAYOUT, SL_STICK_LAYOUT_LEGACY_SOUTHPAW);
    TPROBE();
    ck(ch_walk == -28 && ch_strafe == 28 && ch_turn == 9 && ch_pitch == sgn * 9,
       "LEGACY SOUTHPAW: RY walk -28 / LX strafe 28 (15%), RX turn 9 / LY pitch 9 (30%, x0.5)", st());
    pad_axes(7777, 10000, 10000, 7777); poll();
    ck(ch_strafe == 7 && ch_pitch == 0 && ch_turn == 0 && ch_walk == -7,
       "LEGACY SOUTHPAW: LX 7777 strafe 7, LY 10000 pitch dead, RX 10000 turn dead, RY 7777 walk -7", st());
    stub_aim = 1; TPROBE();
    ck(ch_walk == 0 && ch_strafe == 0 && ch_turn == 9 && ch_pitch == sgn * 9,
       "LEGACY SOUTHPAW aiming: the tuned look pair stays live, the move pair withheld", st());
    stub_aim = 0;
    sl_settings_set(SL_SET_PAD_STICK_LAYOUT, SL_STICK_LAYOUT_DEFAULT);
    sl_settings_set(SL_SET_PAD_LOOK_SENSITIVITY, 100);
    sl_settings_set(SL_SET_PAD_LOOK_DEADZONE, 15);
    TPROBE();
    ck(ch_strafe == 28 && ch_walk == -28 && ch_turn == 28 && ch_pitch == sgn * 28,
       "defaults and DEFAULT restored: 28 on every channel again", st());
    ck((out_b & SL_BTN_CMASK) == 0 && out_x == 0 && out_y == 0 && !ml_on,
       "throughout: no C bit, the N64 stick neutral, no mouse look", st());
#undef TPROBE
    pad_axes(0, 0, 0, 0); poll();

    /* ---- the negative controls --------------------------------------- */
    printf("\n== NEGATIVE CONTROLS under a tuned pad: d-pad, bumpers, mouse, keyboard ==\n");
    sl_settings_set(SL_SET_PAD_LOOK_SENSITIVITY, 200);
    sl_settings_set(SL_SET_PAD_LOOK_DEADZONE, 30);
    sl_settings_set(SL_SET_PAD_MOVE_DEADZONE, 30);
    ck(held_after_button(SDL_CONTROLLER_BUTTON_DPAD_LEFT) == (1u << ACT_WPREV)
       && held_after_button(SDL_CONTROLLER_BUTTON_DPAD_RIGHT) == (1u << ACT_WNEXT)
       && held_after_button(SDL_CONTROLLER_BUTTON_DPAD_UP) == (1u << ACT_ZOOMIN)
       && held_after_button(SDL_CONTROLLER_BUTTON_DPAD_DOWN) == (1u << ACT_ZOOMOUT),
       "d-pad: left / right PREVIOUS / NEXT WEAPON, up / down ZOOM IN / OUT unchanged", "");
    act_clear(); stub_padb[SDL_CONTROLLER_BUTTON_DPAD_UP] = 1; poll();
    ck((out_b & SL_BTN_DUP) == 0 && act_pulse[ACT_ZOOMIN] == 1, "d-pad up in play: no N64 D-UP, one ZOOM IN edge", st());
    stub_padb[SDL_CONTROLLER_BUTTON_DPAD_UP] = 0; poll(); act_clear();
    stub_scoped = 0;
    ck(held_after_button(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) == (1u << ACT_FIRE)
       && held_after_button(SDL_CONTROLLER_BUTTON_LEFTSHOULDER) == (1u << ACT_AIM),
       "LB / RB unscoped: FIRE / AIM (2026-09-21: the bumpers mirror the triggers)", "");
    stub_scoped = 1; poll(); act_clear();
    ck(held_after_button(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) == (1u << ACT_FIRE)
       && held_after_button(SDL_CONTROLLER_BUTTON_LEFTSHOULDER) == (1u << ACT_AIM),
       "LB / RB scoped: FIRE / AIM, the same (an unpaired action has no context)", "");
    pad_axes(0, 0, 16384, 0); poll();
    ck(ch_turn == 39, "scoped: the pad's look channel is the same number as unscoped (39 at 30% x2) - the game's fovy/60 is the only scope scaling", st());
    stub_scoped = 0; pad_axes(0, 0, 0, 0); poll(); act_clear();
    /* The mouse: captured, its look and its sensitivity are the mouse's. */
    sl_input_live_click(SDL_BUTTON_LEFT, 1); poll(); poll();
    motion(40, 0, 0, 0);
    ck(ml_on == 1 && ml_yaw > 5.99f && ml_yaw < 6.01f && ch_turn == 70,
       "mouse +40 counts -> 6.00 degrees of linear look, fallback turn 70 (the mouse-sens numbers; the pad's gain not applied)", st());
    sl_settings_set(SL_SET_MOUSE_SENSITIVITY, 50);
    motion(40, 0, 0, 0);
    ck(ml_on == 1 && ml_yaw > 2.99f && ml_yaw < 3.01f, "mouse_sensitivity 50 -> 3.00 degrees (the mouse's own row, unchanged)", st());
    sl_settings_set(SL_SET_MOUSE_SENSITIVITY, 100);
    press(SDL_SCANCODE_W, 0, 0);
    ck(ch_on == 1 && ch_walk == 70 && ch_turn == 0, "W walks 70 through the keyboard (no deadzone, no gain)", st());
    reset(); poll();
    pad_axes(0, 0, 18884, 0); poll();
    ck(ch_on == 1 && ch_turn == 54, "the pad takes the poll back: 18884 at 30% x2 -> 54", st());
    pad_axes(0, 0, 0, 0); poll();
    sl_settings_set(SL_SET_PAD_LOOK_SENSITIVITY, 100);
    sl_settings_set(SL_SET_PAD_LOOK_DEADZONE, 15);
    sl_settings_set(SL_SET_PAD_MOVE_DEADZONE, 15);

    /* ---- no pad ---------------------------------------------------- */
    stub_pad_present = 0; sl_input_live_device_change(); reset(); poll();
    press(SDL_SCANCODE_W, 0, 0);
    ck(ch_on == 1 && ch_walk == 70, "pad removed: W walks 70", st());
    reset(); poll();

    remove(path);
    printf("\n%d checks, %d failed\n", checks, fails);
    return fails != 0;
#undef NORACLE
}

/* HOLD / TOGGLE (#56, 2026-09-20): the CROUCH and SPRINT modes at the
 * action layer (sl_action_modes_apply), in its OWN PROCESS (INPUTTEST_CASE=
 * hold-toggle), store active on a scratch config written BEFORE init with
 * a malformed line per key so the HOLD fallback is the first thing
 * asserted. What is pinned, against the 68fd4d1e table (the holdtoggle
 * round's witness-pre.txt, 92 rows, byte-identical to the post-edit run
 * with no config):
 *   - HOLD IDENTITY: under the default the published CROUCH / SPRINT level
 *     is the raw level poll for poll (press +P, held, release 0), the
 *     movement channels for W / W+Shift / W+D / W+D+Shift are 70/0, 70/0,
 *     70/70, 70/70, and a press in a menu is unpublished as before;
 *   - CROUCH TOGGLE: OFF, press ON (+P), hold ON ON ON, release ON, idle
 *     ON, press OFF, hold OFF, release OFF; a second bound source (pad B)
 *     flips the same latch; keyboard and pad are semantically identical;
 *   - SPRINT TOGGLE: press latches, release keeps, stopping and resuming
 *     movement keeps, the second press clears; W+D's channels are the same
 *     either way; SPRINT ENABLED off clears the latch and refuses a press,
 *     on again without a press is still off, a fresh press latches again;
 *   - MENU / WATCH ISOLATION: a press in the watch or the front end never
 *     flips; held into a menu and released there leaves the latch; pressed
 *     in a menu and closed held produces no edge until a release and a
 *     fresh press - both actions, both directions; pad B (the watch's
 *     BACK) in the watch never flips CROUCH;
 *   - MODE CHANGE: TOGGLE -> HOLD while latched follows the level; HOLD ->
 *     TOGGLE with the control held starts OFF and wants a release and a
 *     fresh press;
 *   - RESET: sl_action_latch_reset with both latched -> both off, a control
 *     held across it is not a fresh press; sl_action_reset (focus loss)
 *     likewise; the modes are read from the file at init with the latches
 *     off (a restart);
 *   - REMAP: CROUCH on C and SPRINT on pad X follow the action, the old
 *     sources do nothing, a rebind drops the latch (sl_bindings_generation),
 *     RESET DEFAULTS restores Ctrl / Shift / B / L3;
 *   - NEGATIVES: the pad's look channel under the tuning, the mouse's
 *     degrees, the d-pad's zoom level and the bumpers' scope context are
 *     what they were under both modes. */
static int case_hold_toggle(void)
{
    static char env_cfg[600];
    char path[512], t[32];
    const char *tmpdir = getenv("TEMP");
    FILE *f;
    sl_bind_source s;
    int from;
#define CR ((act_held >> ACT_CROUCH) & 1u)
#define SP ((act_held >> ACT_SPRINT) & 1u)
#define CRP (act_pulse[ACT_CROUCH])
#define SPP (act_pulse[ACT_SPRINT])
#define KEY(sc, v) (stub_keys[sc] = (Uint8) (v))
#define PADB(b, v) (stub_padb[b] = (Uint8) (v))
#define STEP() do { act_clear(); poll(); } while (0)

    if (tmpdir == NULL || tmpdir[0] == '\0') tmpdir = ".";
    snprintf(path, sizeof path, "%s\\sl_inputtest_holdtoggle.ini", tmpdir);
    remove(path);
    f = fopen(path, "w");
    if (f) {
        fputs("version=1\ncrouch_mode=toggle\nsprint_mode=\nsprint_enabled=1\n", f);
        fclose(f);
    }
    snprintf(env_cfg, sizeof env_cfg, "SL_CONFIG=%s", path);
    putenv(env_cfg);

    printf("\n== HOLD / TOGGLE (#56): the CROUCH and SPRINT modes at the action layer ==\n");
    sl_settings_init();
    sl_bindings_reload();
    ck(sl_settings_active() && sl_settings_get(SL_SET_CROUCH_MODE) == SL_ACTION_MODE_HOLD
       && sl_settings_get(SL_SET_SPRINT_MODE) == SL_ACTION_MODE_HOLD && sl_settings_get(SL_SET_SPRINT_ENABLED) == 1,
       "malformed crouch_mode / sprint_mode -> HOLD / HOLD, sprint_enabled=1 read beside them", path);

    sl_input_live_set_window((void *) &checks);
    stub_win = (SDL_Window *) &checks;
    sl_input_live_focus(1);
    sl_input_live_click(SDL_BUTTON_LEFT, 1);
    stub_menu = 0; stub_aim = 0; stub_style = 0; stub_scoped = 0;
    reset(); poll();
    pad_attach(SDL_CONTROLLER_TYPE_XBOXONE);
    memset(stub_keys, 0, sizeof stub_keys);
    STEP();

    /* ---- HOLD identity (the 68fd4d1e table) ---------------------------- */
    printf("\n== HOLD (the default): the published level is the raw level, poll for poll ==\n");
    KEY(SDL_SCANCODE_LCTRL, 1); STEP();
    ck(act_on == 1 && CR == 1 && CRP == 1 && sl_action_latched(SL_ACT_CROUCH) == 0, "HOLD: Ctrl down -> CROUCH held, one edge, no latch", st());
    STEP(); ck(CR == 1 && CRP == 0, "HOLD: Ctrl held -> held, no edge", st());
    STEP(); ck(CR == 1 && CRP == 0, "HOLD: Ctrl held -> held (x2)", st());
    KEY(SDL_SCANCODE_LCTRL, 0); STEP();
    ck(CR == 0 && CRP == 0, "HOLD: Ctrl up -> CROUCH not held (the level)", st());
    STEP(); ck(CR == 0, "HOLD: idle -> not held", st());
    PADB(SDL_CONTROLLER_BUTTON_B, 1); STEP();
    ck(CR == 1 && CRP == 1, "HOLD: pad B down -> CROUCH held, one edge", st());
    PADB(SDL_CONTROLLER_BUTTON_B, 0); STEP();
    ck(CR == 0, "HOLD: pad B up -> not held", st());
    KEY(SDL_SCANCODE_W, 1); STEP();
    ck(SP == 0 && ch_walk == 70 && ch_strafe == 0, "HOLD: W -> walk 70, no sprint", st());
    KEY(SDL_SCANCODE_LSHIFT, 1); STEP();
    ck(SP == 1 && SPP == 1 && ch_walk == 70 && ch_strafe == 0, "HOLD: W+Shift -> SPRINT held (one edge), walk 70", st());
    STEP(); ck(SP == 1 && SPP == 0 && ch_walk == 70, "HOLD: W+Shift held -> still held, no edge", st());
    KEY(SDL_SCANCODE_LSHIFT, 0); STEP();
    ck(SP == 0 && ch_walk == 70, "HOLD: Shift up -> SPRINT not held, walk 70", st());
    KEY(SDL_SCANCODE_D, 1); STEP();
    ck(SP == 0 && ch_walk == 70 && ch_strafe == 70, "HOLD: W+D -> walk 70 strafe 70", st());
    KEY(SDL_SCANCODE_LSHIFT, 1); STEP();
    ck(SP == 1 && SPP == 1 && ch_walk == 70 && ch_strafe == 70, "HOLD: W+D+Shift -> SPRINT held, walk 70 strafe 70 (unchanged)", st());
    KEY(SDL_SCANCODE_LSHIFT, 0); KEY(SDL_SCANCODE_W, 0); KEY(SDL_SCANCODE_D, 0); STEP();
    ck(SP == 0 && ch_walk == 0 && ch_strafe == 0, "HOLD: all up -> nothing held", st());
    stub_menu = 1; KEY(SDL_SCANCODE_LCTRL, 1); STEP();
    ck(act_on == 0 && CR == 1 && CRP == 1, "HOLD: Ctrl in the watch -> evaluated (held, edge) but unpublished (on=0), as before", st());
    KEY(SDL_SCANCODE_LCTRL, 0); STEP(); stub_menu = 0; STEP();
    ck(act_on == 1 && CR == 0, "HOLD: released in the watch, watch closed -> not held", st());

    /* ---- CROUCH TOGGLE ------------------------------------------------- */
    printf("\n== CROUCH TOGGLE: press ON, hold, release ON, press OFF ==\n");
    sl_settings_set(SL_SET_CROUCH_MODE, SL_ACTION_MODE_TOGGLE);
    STEP();
    ck(sl_settings_get(SL_SET_CROUCH_MODE) == 1 && CR == 0 && sl_action_latched(SL_ACT_CROUCH) == 0, "TOGGLE set: starts OFF", st());
    KEY(SDL_SCANCODE_LCTRL, 1); STEP();
    ck(CR == 1 && CRP == 1 && sl_action_latched(SL_ACT_CROUCH) == 1, "press -> ON (+P), latched", st());
    STEP(); ck(CR == 1 && CRP == 0, "hold -> ON, no edge", st());
    STEP(); ck(CR == 1 && CRP == 0, "hold -> ON (x2)", st());
    STEP(); ck(CR == 1 && CRP == 0, "hold -> ON (x3): holding never repeats", st());
    KEY(SDL_SCANCODE_LCTRL, 0); STEP();
    ck(CR == 1 && CRP == 0, "release -> still ON: release never toggles", st());
    STEP(); ck(CR == 1, "idle -> ON", st());
    STEP(); ck(CR == 1, "idle -> ON (x2)", st());
    KEY(SDL_SCANCODE_LCTRL, 1); STEP();
    ck(CR == 0 && CRP == 1 && sl_action_latched(SL_ACT_CROUCH) == 0, "second press -> OFF (+P), latch cleared", st());
    STEP(); ck(CR == 0 && CRP == 0, "hold -> OFF", st());
    KEY(SDL_SCANCODE_LCTRL, 0); STEP();
    ck(CR == 0, "release -> OFF", st());
    STEP(); ck(CR == 0, "idle -> OFF", st());
    printf("\n== CROUCH TOGGLE: the pad's B is the same latch ==\n");
    PADB(SDL_CONTROLLER_BUTTON_B, 1); STEP();
    ck(CR == 1 && CRP == 1, "pad B press -> ON", st());
    STEP(); ck(CR == 1 && CRP == 0, "pad B held -> ON", st());
    PADB(SDL_CONTROLLER_BUTTON_B, 0); STEP();
    ck(CR == 1, "pad B release -> ON", st());
    KEY(SDL_SCANCODE_LCTRL, 1); STEP(); KEY(SDL_SCANCODE_LCTRL, 0); STEP();
    ck(CR == 0, "Ctrl press / release -> OFF: the second bound source flips the same latch", st());
    KEY(SDL_SCANCODE_LCTRL, 1); STEP(); KEY(SDL_SCANCODE_LCTRL, 0); STEP();
    ck(CR == 1, "Ctrl again -> ON", st());
    PADB(SDL_CONTROLLER_BUTTON_B, 1); STEP(); PADB(SDL_CONTROLLER_BUTTON_B, 0); STEP();
    ck(CR == 0, "pad B -> OFF (keyboard and controller semantically identical)", st());
    KEY(SDL_SCANCODE_LCTRL, 1); STEP();
    PADB(SDL_CONTROLLER_BUTTON_B, 1); STEP();
    ck(CR == 1 && CRP == 0, "B pressed while Ctrl is held -> no new edge (the action's level was already up), still ON", st());
    KEY(SDL_SCANCODE_LCTRL, 0); PADB(SDL_CONTROLLER_BUTTON_B, 0); STEP();
    ck(CR == 1, "both released -> ON", st());
    KEY(SDL_SCANCODE_LCTRL, 1); STEP(); KEY(SDL_SCANCODE_LCTRL, 0); STEP();
    ck(CR == 0, "Ctrl -> OFF", st());

    /* ---- menu / watch isolation, CROUCH ---------------------------------- */
    printf("\n== CROUCH TOGGLE: menu and watch isolation ==\n");
    stub_menu = 1; STEP();
    KEY(SDL_SCANCODE_LCTRL, 1); STEP();
    ck(act_on == 0 && CR == 0 && sl_action_latched(SL_ACT_CROUCH) == 0, "Ctrl pressed in the watch -> no flip (unpublished, no latch)", st());
    KEY(SDL_SCANCODE_LCTRL, 0); STEP();
    PADB(SDL_CONTROLLER_BUTTON_B, 1); STEP();
    ck(act_on == 0 && CR == 0 && (out_b & SL_BTN_B) != 0, "pad B in the watch -> the watch's BACK (N64 B), no flip", st());
    PADB(SDL_CONTROLLER_BUTTON_B, 0); STEP();
    stub_menu = 0; STEP();
    ck(act_on == 1 && CR == 0, "watch closed -> still OFF", st());
    stub_menu = 2; STEP();
    KEY(SDL_SCANCODE_LCTRL, 1); STEP(); KEY(SDL_SCANCODE_LCTRL, 0); STEP();
    PADB(SDL_CONTROLLER_BUTTON_B, 1); STEP(); PADB(SDL_CONTROLLER_BUTTON_B, 0); STEP();
    stub_menu = 0; STEP();
    ck(CR == 0, "Ctrl and pad B in the front end -> no flip", st());
    KEY(SDL_SCANCODE_RETURN, 1); STEP(); KEY(SDL_SCANCODE_RETURN, 0); STEP();
    KEY(SDL_SCANCODE_SPACE, 1); STEP(); KEY(SDL_SCANCODE_SPACE, 0); STEP();
    ck(CR == 0, "Enter and Space in play -> not CROUCH sources, no flip", st());
    printf("\n== CROUCH TOGGLE: held across a context change, both directions ==\n");
    KEY(SDL_SCANCODE_LCTRL, 1); STEP();
    ck(CR == 1, "Ctrl press in play -> ON", st());
    stub_menu = 1; STEP();
    ck(act_on == 0 && sl_action_latched(SL_ACT_CROUCH) == 1, "watch opens with Ctrl held -> latch kept (unpublished)", st());
    KEY(SDL_SCANCODE_LCTRL, 0); STEP();
    ck(sl_action_latched(SL_ACT_CROUCH) == 1, "Ctrl released in the watch -> latch kept", st());
    stub_menu = 0; STEP();
    ck(act_on == 1 && CR == 1 && CRP == 0, "watch closed -> ON, no edge: open / close never toggles or clears", st());
    stub_menu = 1; STEP();
    KEY(SDL_SCANCODE_LCTRL, 1); STEP(); STEP();
    ck(sl_action_latched(SL_ACT_CROUCH) == 1, "Ctrl pressed and held in the watch -> no flip", st());
    stub_menu = 0; STEP();
    ck(CR == 1 && CRP == 0, "watch closed with Ctrl held -> no edge, still ON", st());
    STEP(); ck(CR == 1 && CRP == 0, "Ctrl still held in play -> still ON", st());
    KEY(SDL_SCANCODE_LCTRL, 0); STEP();
    ck(CR == 1, "Ctrl released -> still ON", st());
    KEY(SDL_SCANCODE_LCTRL, 1); STEP();
    ck(CR == 0 && CRP == 1, "fresh press -> OFF", st());
    KEY(SDL_SCANCODE_LCTRL, 0); STEP();
    stub_menu = 1; STEP();
    PADB(SDL_CONTROLLER_BUTTON_B, 1); STEP();
    stub_menu = 0; STEP();
    ck(CR == 0 && CRP == 0, "pad B held as the watch's BACK through the close -> no edge, OFF", st());
    PADB(SDL_CONTROLLER_BUTTON_B, 0); STEP();
    PADB(SDL_CONTROLLER_BUTTON_B, 1); STEP();
    ck(CR == 1 && CRP == 1, "pad B released and pressed afresh -> ON", st());
    PADB(SDL_CONTROLLER_BUTTON_B, 0); STEP();

    /* ---- mode change at runtime ------------------------------------------ */
    printf("\n== CROUCH: mode change at runtime ==\n");
    ck(CR == 1, "latched ON", st());
    sl_settings_set(SL_SET_CROUCH_MODE, SL_ACTION_MODE_HOLD); STEP();
    ck(CR == 0 && sl_action_latched(SL_ACT_CROUCH) == 0, "TOGGLE -> HOLD with nothing held -> OFF (the level), latch dropped", st());
    KEY(SDL_SCANCODE_LCTRL, 1); STEP();
    ck(CR == 1, "HOLD: Ctrl down -> held", st());
    sl_settings_set(SL_SET_CROUCH_MODE, SL_ACTION_MODE_TOGGLE); STEP();
    ck(CR == 0 && CRP == 0 && sl_action_latched(SL_ACT_CROUCH) == 0, "HOLD -> TOGGLE with Ctrl held -> OFF, no edge (no stuck crouch)", st());
    STEP(); ck(CR == 0, "Ctrl still held -> OFF", st());
    KEY(SDL_SCANCODE_LCTRL, 0); STEP();
    ck(CR == 0, "Ctrl released -> OFF", st());
    KEY(SDL_SCANCODE_LCTRL, 1); STEP();
    ck(CR == 1 && CRP == 1, "fresh press -> ON", st());
    sl_settings_set(SL_SET_CROUCH_MODE, SL_ACTION_MODE_HOLD); STEP();
    ck(CR == 1 && sl_action_latched(SL_ACT_CROUCH) == 0, "TOGGLE -> HOLD with Ctrl held -> held (the level), latch dropped", st());
    KEY(SDL_SCANCODE_LCTRL, 0); STEP();
    ck(CR == 0, "HOLD: Ctrl up -> not held", st());
    sl_settings_set(SL_SET_CROUCH_MODE, SL_ACTION_MODE_TOGGLE); STEP();
    ck(CR == 0, "back to TOGGLE -> OFF", st());

    /* ---- SPRINT TOGGLE ------------------------------------------------ */
    printf("\n== SPRINT TOGGLE: latch through press, release, stop, resume, second press ==\n");
    sl_settings_set(SL_SET_SPRINT_MODE, SL_ACTION_MODE_TOGGLE); STEP();
    ck(SP == 0 && sl_action_latched(SL_ACT_SPRINT) == 0, "TOGGLE set: SPRINT starts OFF", st());
    KEY(SDL_SCANCODE_W, 1); STEP();
    ck(SP == 0 && ch_walk == 70, "W -> walk 70, not sprinting", st());
    KEY(SDL_SCANCODE_LSHIFT, 1); STEP();
    ck(SP == 1 && SPP == 1 && ch_walk == 70 && ch_strafe == 0, "Shift press -> SPRINT ON (+P), walk 70", st());
    KEY(SDL_SCANCODE_LSHIFT, 0); STEP();
    ck(SP == 1 && SPP == 0 && ch_walk == 70, "Shift release -> still ON, walk 70", st());
    KEY(SDL_SCANCODE_W, 0); STEP();
    ck(SP == 1 && ch_walk == 0, "W up (stop moving) -> still ON, walk 0: no auto-cancel", st());
    STEP(); ck(SP == 1, "stationary -> still ON", st());
    KEY(SDL_SCANCODE_W, 1); STEP();
    ck(SP == 1 && ch_walk == 70, "W again (resume) -> ON, walk 70: applies again", st());
    KEY(SDL_SCANCODE_D, 1); STEP();
    ck(SP == 1 && ch_walk == 70 && ch_strafe == 70, "W+D latched -> walk 70 strafe 70 (the channels unchanged)", st());
    KEY(SDL_SCANCODE_LSHIFT, 1); STEP();
    ck(SP == 0 && SPP == 1 && ch_walk == 70 && ch_strafe == 70, "second press (W+D held) -> OFF, walk 70 strafe 70", st());
    KEY(SDL_SCANCODE_LSHIFT, 0); STEP();
    ck(SP == 0, "release -> OFF", st());
    KEY(SDL_SCANCODE_W, 0); KEY(SDL_SCANCODE_D, 0); STEP();
    KEY(SDL_SCANCODE_LSHIFT, 1); STEP(); KEY(SDL_SCANCODE_LSHIFT, 0); STEP();
    ck(SP == 1 && ch_walk == 0, "Shift press while still -> ON (may stay on while stationary)", st());
    PADB(SDL_CONTROLLER_BUTTON_LEFTSTICK, 1); STEP(); PADB(SDL_CONTROLLER_BUTTON_LEFTSTICK, 0); STEP();
    ck(SP == 0, "pad L3 -> OFF: the second bound source flips the same latch", st());
    PADB(SDL_CONTROLLER_BUTTON_LEFTSTICK, 1); STEP();
    ck(SP == 1 && SPP == 1, "pad L3 press -> ON", st());
    STEP(); ck(SP == 1 && SPP == 0, "pad L3 held -> ON, no edge", st());
    PADB(SDL_CONTROLLER_BUTTON_LEFTSTICK, 0); STEP();
    ck(SP == 1, "pad L3 release -> ON", st());
    printf("\n== SPRINT TOGGLE: SPRINT ENABLED off clears the latch ==\n");
    sl_settings_set(SL_SET_SPRINT_ENABLED, 0); STEP();
    ck(SP == 0 && sl_action_latched(SL_ACT_SPRINT) == 0, "sprint_enabled off -> latch cleared, SPRINT 0", st());
    KEY(SDL_SCANCODE_LSHIFT, 1); STEP(); KEY(SDL_SCANCODE_LSHIFT, 0); STEP();
    ck(SP == 0 && sl_action_latched(SL_ACT_SPRINT) == 0, "a press while disabled -> nothing latched", st());
    sl_settings_set(SL_SET_SPRINT_ENABLED, 1); STEP();
    ck(SP == 0, "sprint_enabled on again without a press -> still OFF (no resumed sprint)", st());
    KEY(SDL_SCANCODE_LSHIFT, 1); STEP(); KEY(SDL_SCANCODE_LSHIFT, 0); STEP();
    ck(SP == 1, "fresh press -> ON again", st());
    printf("\n== SPRINT TOGGLE: menu and watch isolation, both directions ==\n");
    stub_menu = 1; STEP();
    ck(act_on == 0 && sl_action_latched(SL_ACT_SPRINT) == 1, "watch opens -> latch kept", st());
    KEY(SDL_SCANCODE_LSHIFT, 1); STEP(); KEY(SDL_SCANCODE_LSHIFT, 0); STEP();
    ck(sl_action_latched(SL_ACT_SPRINT) == 1, "Shift press in the watch -> no flip", st());
    stub_menu = 0; STEP();
    ck(SP == 1 && SPP == 0, "watch closed -> ON, no edge", st());
    KEY(SDL_SCANCODE_LSHIFT, 1); STEP();
    ck(SP == 0, "fresh press -> OFF", st());
    stub_menu = 1; STEP();
    KEY(SDL_SCANCODE_LSHIFT, 0); STEP();
    stub_menu = 0; STEP();
    ck(SP == 0 && SPP == 0, "held into the watch, released there, closed -> OFF, no edge", st());
    stub_menu = 1; STEP();
    PADB(SDL_CONTROLLER_BUTTON_LEFTSTICK, 1); STEP();
    stub_menu = 0; STEP();
    ck(SP == 0 && SPP == 0, "L3 pressed in the watch, closed held -> no edge, OFF", st());
    STEP(); ck(SP == 0, "L3 still held -> OFF", st());
    PADB(SDL_CONTROLLER_BUTTON_LEFTSTICK, 0); STEP();
    PADB(SDL_CONTROLLER_BUTTON_LEFTSTICK, 1); STEP();
    ck(SP == 1 && SPP == 1, "L3 released and pressed afresh -> ON", st());
    PADB(SDL_CONTROLLER_BUTTON_LEFTSTICK, 0); STEP();
    printf("\n== SPRINT: mode change at runtime ==\n");
    KEY(SDL_SCANCODE_LSHIFT, 1); STEP();
    ck(SP == 0, "Shift press -> OFF (was ON)", st());
    sl_settings_set(SL_SET_SPRINT_MODE, SL_ACTION_MODE_HOLD); STEP();
    ck(SP == 1 && sl_action_latched(SL_ACT_SPRINT) == 0, "TOGGLE -> HOLD with Shift held -> held (the level)", st());
    sl_settings_set(SL_SET_SPRINT_MODE, SL_ACTION_MODE_TOGGLE); STEP();
    ck(SP == 0 && SPP == 0, "HOLD -> TOGGLE with Shift held -> OFF, no edge", st());
    KEY(SDL_SCANCODE_LSHIFT, 0); STEP(); KEY(SDL_SCANCODE_LSHIFT, 1); STEP();
    ck(SP == 1 && SPP == 1, "release + fresh press -> ON", st());
    KEY(SDL_SCANCODE_LSHIFT, 0); STEP();

    /* ---- resets ---------------------------------------------------------- */
    printf("\n== resets: a new stage (sl_action_latch_reset) and a focus loss (sl_action_reset) ==\n");
    KEY(SDL_SCANCODE_LCTRL, 1); STEP();
    ck(CR == 1 && SP == 1 && sl_action_latched(SL_ACT_CROUCH) == 1 && sl_action_latched(SL_ACT_SPRINT) == 1, "both latched ON, Ctrl still held", st());
    sl_action_latch_reset(); STEP();
    ck(CR == 0 && SP == 0 && CRP == 0 && sl_action_latched(SL_ACT_CROUCH) == 0 && sl_action_latched(SL_ACT_SPRINT) == 0,
       "new stage -> both OFF; the held Ctrl is not a fresh press", st());
    STEP(); ck(CR == 0, "Ctrl still held -> OFF", st());
    KEY(SDL_SCANCODE_LCTRL, 0); STEP(); KEY(SDL_SCANCODE_LCTRL, 1); STEP();
    ck(CR == 1, "release + fresh press -> ON", st());
    KEY(SDL_SCANCODE_LCTRL, 0); STEP();
    KEY(SDL_SCANCODE_LSHIFT, 1); STEP(); KEY(SDL_SCANCODE_LSHIFT, 0); STEP();
    ck(CR == 1 && SP == 1, "both ON again", st());
    sl_input_live_focus(0); sl_input_live_focus(1); sl_input_live_click(SDL_BUTTON_LEFT, 1); STEP();
    ck(CR == 0 && SP == 0 && sl_action_latched(SL_ACT_CROUCH) == 0 && sl_action_latched(SL_ACT_SPRINT) == 0,
       "focus loss -> both OFF (no stuck crouch or sprint)", st());
    ck(sl_settings_get(SL_SET_CROUCH_MODE) == 1 && sl_settings_get(SL_SET_SPRINT_MODE) == 1, "the modes survive both resets (they are settings, the latches are not)", "");

    /* ---- remap ----------------------------------------------------------- */
    printf("\n== remap: the latch belongs to the action ==\n");
    KEY(SDL_SCANCODE_LCTRL, 1); STEP(); KEY(SDL_SCANCODE_LCTRL, 0); STEP();
    ck(CR == 1, "Ctrl -> ON", st());
    sl_bindings_source_parse("key:C", &s);
    sl_bindings_set(SL_ACT_CROUCH, 0, 0, &s, &from); STEP();
    ck(!strcmp(sl_bindings_source_token(sl_bindings_get(SL_ACT_CROUCH, 0, 0), t, 32), "key:C") && CR == 0 && sl_action_latched(SL_ACT_CROUCH) == 0,
       "CROUCH rebound to C -> the latch is dropped with the old source", st());
    KEY(SDL_SCANCODE_LCTRL, 1); STEP(); KEY(SDL_SCANCODE_LCTRL, 0); STEP();
    ck(CR == 0, "Ctrl (no longer bound) -> nothing", st());
    KEY(SDL_SCANCODE_C, 1); STEP();
    ck(CR == 1 && CRP == 1, "C press -> ON", st());
    KEY(SDL_SCANCODE_C, 0); STEP(); ck(CR == 1, "C release -> ON", st());
    KEY(SDL_SCANCODE_C, 1); STEP(); KEY(SDL_SCANCODE_C, 0); STEP();
    ck(CR == 0, "C again -> OFF", st());
    PADB(SDL_CONTROLLER_BUTTON_B, 1); STEP(); PADB(SDL_CONTROLLER_BUTTON_B, 0); STEP();
    ck(CR == 1, "pad B (still bound) -> ON: the same latch as C", st());
    sl_bindings_source_parse("pad:X", &s);
    sl_bindings_set(SL_ACT_SPRINT, 1, 0, &s, &from); STEP();
    ck(!strcmp(sl_bindings_source_token(sl_bindings_get(SL_ACT_SPRINT, 1, 0), t, 32), "pad:X") && CR == 1,
       "SPRINT's pad slot rebound to X -> CROUCH's latch untouched", st());
    PADB(SDL_CONTROLLER_BUTTON_LEFTSTICK, 1); STEP(); PADB(SDL_CONTROLLER_BUTTON_LEFTSTICK, 0); STEP();
    ck(SP == 0, "pad L3 (no longer SPRINT) -> nothing", st());
    PADB(SDL_CONTROLLER_BUTTON_X, 1); STEP(); PADB(SDL_CONTROLLER_BUTTON_X, 0); STEP();
    ck(SP == 1, "pad X press / release -> SPRINT ON", st());
    KEY(SDL_SCANCODE_LSHIFT, 1); STEP(); KEY(SDL_SCANCODE_LSHIFT, 0); STEP();
    ck(SP == 0, "Shift (the keyboard slot, still bound) -> OFF: the same latch as X", st());
    sl_bindings_reset_defaults(); STEP();
    ck(!strcmp(sl_bindings_source_token(sl_bindings_get(SL_ACT_CROUCH, 0, 0), t, 32), "key:LCTRL")
       && !strcmp(sl_bindings_source_token(sl_bindings_get(SL_ACT_SPRINT, 1, 0), t, 32), "pad:LS")
       && CR == 0 && SP == 0,
       "RESET DEFAULTS -> Ctrl / L3 again, both latches dropped", st());
    KEY(SDL_SCANCODE_C, 1); STEP(); KEY(SDL_SCANCODE_C, 0); STEP();
    ck(CR == 0, "C (no longer bound) -> nothing", st());
    KEY(SDL_SCANCODE_LCTRL, 1); STEP(); KEY(SDL_SCANCODE_LCTRL, 0); STEP();
    ck(CR == 1, "Ctrl -> ON again", st());
    PADB(SDL_CONTROLLER_BUTTON_LEFTSTICK, 1); STEP(); PADB(SDL_CONTROLLER_BUTTON_LEFTSTICK, 0); STEP();
    ck(SP == 1, "L3 -> SPRINT ON again", st());
    ck(sl_settings_get(SL_SET_CROUCH_MODE) == 1 && sl_settings_get(SL_SET_SPRINT_MODE) == 1, "RESET DEFAULTS leaves the modes alone (bindings only)", "");

    /* ---- negatives ------------------------------------------------------- */
    printf("\n== negatives: the analog tuning, the mouse, the d-pad and the bumpers under both TOGGLE modes ==\n");
    pad_axes(0, 0, 16384, 0); STEP();
    ck(ch_turn == 28 && ch_pitch == 0 && CR == 1 && SP == 1, "RX 16384 -> turn 28 (the 04b92554 channel), the latches untouched", st());
    pad_axes(0, 0, 0, 0); STEP();
    stub_dx = 40; STEP();
    ck(ml_on == 1 && ml_yaw > 5.99f && ml_yaw < 6.01f, "mouse dx 40 -> 6.00 degrees", st());
    PADB(SDL_CONTROLLER_BUTTON_DPAD_UP, 1); STEP();
    ck((act_held & (1u << ACT_ZOOMIN)) != 0 && CR == 1, "d-pad up -> ZOOM IN held (a level), CROUCH untouched", st());
    PADB(SDL_CONTROLLER_BUTTON_DPAD_UP, 0); STEP();
    PADB(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 1); STEP();
    ck((act_held & (1u << ACT_FIRE)) != 0 && (act_held & (1u << ACT_WNEXT)) == 0 && (act_held & (1u << ACT_ZOOMIN)) == 0,
       "RB on foot -> FIRE (2026-09-21: the bumpers mirror the triggers)", st());
    PADB(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 0); STEP();
    stub_scoped = 1; PADB(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 1); STEP();
    ck((act_held & (1u << ACT_FIRE)) != 0 && (act_held & (1u << ACT_ZOOMIN)) == 0, "RB scoped -> STILL FIRE", st());
    PADB(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 0); stub_scoped = 0; STEP();
    ck(CR == 1 && SP == 1 && sl_action_latched(SL_ACT_CROUCH) == 1 && sl_action_latched(SL_ACT_SPRINT) == 1,
       "after all of it: both latches still ON", st());
    KEY(SDL_SCANCODE_LCTRL, 1); STEP(); KEY(SDL_SCANCODE_LCTRL, 0); STEP();
    KEY(SDL_SCANCODE_LSHIFT, 1); STEP(); KEY(SDL_SCANCODE_LSHIFT, 0); STEP();
    ck(CR == 0 && SP == 0, "one press each -> both OFF", st());

    /* ---- the file ---------------------------------------------------------- */
    {
        char text[4096]; size_t n = 0;
        f = fopen(path, "r");
        if (f) { n = fread(text, 1, sizeof text - 1, f); fclose(f); }
        text[n] = '\0';
        ck(strstr(text, "crouch_mode=1\n") != NULL && strstr(text, "sprint_mode=1\n") != NULL && strstr(text, "sprint_enabled=1\n") != NULL,
           "the file holds crouch_mode=1 sprint_mode=1 sprint_enabled=1 (the modes persist; no latch is written)", "");
        ck(strstr(text, "latch") == NULL, "nothing about a latch in the file", "");
    }

    remove(path);
    printf("\n%d checks, %d failed\n", checks, fails);
    return fails != 0;
#undef CR
#undef SP
#undef CRP
#undef SPP
#undef KEY
#undef PADB
#undef STEP
}

int main(void)
{
    int i, steps_up, steps_down, saw_centre;
    const char *one_case = getenv("INPUTTEST_CASE");

    if (one_case != NULL && strcmp(one_case, "mouse-invert-store") == 0)
        return case_mouse_invert(1);
    if (one_case != NULL && strcmp(one_case, "mouse-invert-nostore") == 0)
        return case_mouse_invert(0);
    if (one_case != NULL && strcmp(one_case, "mouse-sens") == 0)
        return case_mouse_sens();
    if (one_case != NULL && strcmp(one_case, "modern-pad") == 0)
        return case_modern_pad();
    if (one_case != NULL && strcmp(one_case, "pad-tune") == 0)
        return case_pad_tune(0);
    if (one_case != NULL && strcmp(one_case, "pad-tune-invert") == 0)
        return case_pad_tune(1);
    if (one_case != NULL && strcmp(one_case, "hold-toggle") == 0)
        return case_hold_toggle();

    /* set_grab only calls SDL_SetWindowMouseGrab when it has a window, exactly as
     * the SDL backend hands one over at init. A dummy non-NULL pointer is
     * enough: the stub above never dereferences it. */
    sl_input_live_set_window((void *) &checks);
    stub_win = (SDL_Window *) &checks;

    /* The pointer has to be captured before read_mouse will look at motion at
     * all - that is click-to-capture, and it is the production behaviour. */
    sl_input_live_focus(1);
    sl_input_live_click(SDL_BUTTON_LEFT, 1);
    stub_menu = 0; stub_aim = 0; stub_style = 0;
    poll();

    printf("\n== capture ==\n");
    ck(stub_relmode == 1 && stub_wingrab == 1 && stub_cursor == 0,
       "click in a focused window captures", st());
    sl_input_live_focus(0); poll();
    ck(stub_relmode == 0 && stub_cursor == 1, "focus loss releases and disarms", st());
    sl_input_live_focus(1); poll();
    ck(stub_relmode == 0, "focus regained alone does NOT re-capture", st());
    sl_input_live_click(SDL_BUTTON_LEFT, 1); poll();
    ck(stub_relmode == 1, "a second click re-captures", st());
    /* #40 (owner, 2026-09-17): the WATCH keeps gameplay's capture - relative
     * mode, hidden cursor - and the deltas drive the drawn crosshair there.
     * Before #40 this asserted the release; the front end (menu 2) is the
     * screen class that releases relative mode, tested below. */
    stub_menu = 1; poll();
    ck(stub_relmode == 1 && stub_cursor == 0, "the watch KEEPS the capture (#40)", st());
    stub_menu = 0; poll();
    ck(stub_relmode == 1 && stub_cursor == 0,
       "leaving the watch stays captured, no click", st());

    /* THE ESCAPE BUG, as a regression test.
     *
     * SDL drops its own mouse grab when the window loses input focus - measured
     * on this machine, SDL_GetWindowGrab going 1 -> 0 with no call from us. The
     * old set_grab cached its intent in g_grabbed and early-returned when the
     * intent had not changed, so once SDL had dropped the grab it was never
     * re-asserted and the pointer was free while everything still reported
     * captured. Simulate exactly that: clear the stub's grab behind the input
     * layer's back, and require the next poll to notice and put it back. */
    printf("\n== the grab is re-asserted, not assumed ==\n");
    stub_menu = 0;
    sl_input_live_click(SDL_BUTTON_LEFT, 1); poll();
    ck(stub_wingrab == 1, "captured before the simulated drop", st());
    stub_wingrab = 0;                  /* SDL drops it on its own */
    poll();
    ck(stub_wingrab == 1, "a grab SDL dropped is re-asserted next poll", st());
    stub_relmode = 0;                  /* and the same for relative mode */
    poll();
    ck(stub_relmode == 1, "relative mode is re-asserted too", st());

    /* THE CONFINEMENT RECTANGLE's lifecycle. The rect itself cannot be tested
     * for physical effect here - only its arming, its shape, and crucially
     * that it is REMOVED on release, which is the failure that would trap the
     * player's desktop pointer. */
    printf("\n== mouse confinement rect ==\n");
    stub_menu = 0;
    sl_input_live_click(SDL_BUTTON_LEFT, 1); poll();
    ck(stub_rect_set && stub_rect.x == 0 && stub_rect.y == 0
       && stub_rect.w == 960 && stub_rect.h == 720,
       "capture arms a full-client-rect barrier, window-relative", st());
    stub_menu = 1; poll();
    ck(stub_rect_set, "the watch keeps the barrier (#40: still captured)", st());
    stub_menu = 0; poll();
    ck(stub_rect_set, "leaving the watch keeps it", st());
    sl_input_live_focus(0); poll();
    ck(!stub_rect_set && stub_relmode == 0,
       "focus loss removes the barrier and never traps the pointer", st());
    sl_input_live_focus(1); sl_input_live_click(SDL_BUTTON_LEFT, 1); poll();
    ck(stub_rect_set, "a click after focus loss re-arms it", st());
    sl_input_live_shutdown();
    ck(!stub_rect_set, "shutdown removes the barrier", st());
    /* back to a captured state for the tests that follow */
    sl_input_live_set_window((void *) &checks);
    sl_input_live_focus(1); sl_input_live_click(SDL_BUTTON_LEFT, 1); poll();

    printf("\n== buttons ==\n");
    /* E and R left the button map on 2026-09-17 (#38): they are ACTIONS now,
     * asserted in their own section below, and produce no N64 button. */
    press(SDL_SCANCODE_E, 0, 0);
    ck(out_b == 0, "E -> no N64 button (it is the INTERACT action)", st());
    press(SDL_SCANCODE_R, 0, 0);
    ck(out_b == 0, "R -> no N64 button (it is the RELOAD action)", st());
    press(SDL_SCANCODE_SPACE, 0, 0);
    ck(out_b == SL_BTN_B, "Space -> N64 B (use / reload)", st());
    press(SDL_SCANCODE_RETURN, 0, 0);
    ck(out_b == SL_BTN_A, "Enter in play -> A, never START", st());
    ck((out_b & SL_BTN_START) == 0, "Enter does NOT open the watch", st());
    press(SDL_SCANCODE_RETURN, 1, 0);
    ck(out_b == SL_BTN_A, "Enter in a menu -> A (confirm)", st());
    press(SDL_SCANCODE_F, 0, 0);
    ck(out_b == SL_BTN_Z, "F -> N64 Z (fire)", st());
    press(SDL_SCANCODE_Q, 0, 0);
    ck(out_b == 0x0010, "Q -> N64 R (aim)", st());

    printf("\n== start: Tab and Escape only ==\n");
    reset(); stub_menu = 0;
    sl_input_live_start(); poll();
    ck((out_b & SL_BTN_START) != 0, "Tab -> N64 START", st());
    for (i = 0; i < 6; i++) poll();
    ck((out_b & SL_BTN_START) == 0, "Tab is one edge, not a hold", st());
    reset(); stub_menu = 0;
    sl_input_live_escape(); poll();
    ck((out_b & SL_BTN_START) != 0, "Escape in play -> START (opens the watch)", st());
    for (i = 0; i < 6; i++) poll();
    reset(); stub_menu = 2;            /* SL_MENU_FRONT */
    sl_input_live_escape(); poll();
    ck((out_b & SL_BTN_B) != 0 && (out_b & SL_BTN_START) == 0,
       "Escape in the front end -> B (back)", st());
    for (i = 0; i < 6; i++) poll();
    stub_menu = 0;

    printf("\n== actions: gameplay semantics, not buttons (#38) ==\n");
    /* Levels and edges. A key held across polls is ONE press; the level stays. */
    act_clear(); reset(); stub_menu = 0; stub_scoped = 0; poll();
    act_clear();
    press(SDL_SCANCODE_E, 0, 0);
    ck(act_on == 1 && (act_held & (1u << ACT_INTERACT)) && act_pulse[ACT_INTERACT] == 1
       && out_b == 0, "E -> INTERACT held + one edge, no button", st());
    poll();                                /* still held */
    ck((act_held & (1u << ACT_INTERACT)) && act_pulse[ACT_INTERACT] == 1,
       "E held across polls -> still held, still ONE edge", st());
    act_clear();
    press(SDL_SCANCODE_R, 0, 0);
    ck((act_held & (1u << ACT_RELOAD)) && act_pulse[ACT_RELOAD] == 1
       && act_pulse[ACT_INTERACT] == 0, "R -> RELOAD, and E's release is not an edge", st());
    act_clear();
    press(SDL_SCANCODE_LCTRL, 0, 0);
    ck((act_held & (1u << ACT_CROUCH)) && act_pulse[ACT_CROUCH] == 1 && out_b == 0,
       "Left Ctrl -> CROUCH held, no button", st());
    act_clear();
    press(SDL_SCANCODE_1, 0, 0);
    ck(act_pulse[ACT_WPREV] == 1 && out_b == 0, "1 -> WEAPON_PREVIOUS edge", st());
    act_clear();
    press(SDL_SCANCODE_2, 0, 0);
    ck(act_pulse[ACT_WNEXT] == 1 && out_b == 0, "2 -> WEAPON_NEXT edge", st());

    /* The wheel's three contexts, in order: menu (stick pulse, no action),
     * scoped (zoom), ordinary play (weapon cycle). */
    act_clear(); reset(); stub_menu = 0; stub_scoped = 0;
    sl_input_live_wheel(2); poll();
    ck(act_pulse[ACT_WPREV] == 2 && act_pulse[ACT_ZOOMIN] == 0 && out_y == 0,
       "wheel up x2 in play -> WEAPON_PREVIOUS x2, no zoom, no stick", st());
    act_clear(); reset();
    sl_input_live_wheel(-1); poll();
    ck(act_pulse[ACT_WNEXT] == 1 && act_pulse[ACT_ZOOMOUT] == 0,
       "wheel down in play -> WEAPON_NEXT", st());
    act_clear(); reset(); stub_scoped = 1;
    sl_input_live_wheel(1); poll();
    ck(act_pulse[ACT_ZOOMIN] == 1 && act_pulse[ACT_WPREV] == 0,
       "wheel up while scoped -> ZOOM_IN, no weapon cycle", st());
    act_clear(); reset();
    sl_input_live_wheel(-3); poll();
    ck(act_pulse[ACT_ZOOMOUT] == 3 && act_pulse[ACT_WNEXT] == 0,
       "wheel down x3 while scoped -> ZOOM_OUT x3", st());
    stub_scoped = 0;
    act_clear(); reset(); stub_menu = 1;
    sl_input_live_wheel(1); poll();
    ck(act_on == 0 && act_pulse[ACT_WPREV] == 0 && act_pulse[ACT_ZOOMIN] == 0 && out_y == 70,
       "wheel in the watch -> the stick pulse, channels off, no action", st());
    for (i = 0; i < 8; i++) poll();
    act_clear(); reset(); stub_menu = 1;
    stub_keys[SDL_SCANCODE_E] = 1; poll();
    ck(act_on == 0 && out_b == 0, "E in a menu -> channels off, no button", st());
    reset(); stub_menu = 0; poll(); act_clear();

    printf("\n== SPRINT (#42): Left Shift is a LEVEL, published regardless of the setting ==\n");
    /* The platform layer knows nothing about sprint_enabled (sl_settings_set
     * is a link stub here and sl_settings_get is not even linked): the action
     * is published whenever the key is down, and the four movement channels
     * are exactly what they are without it. Applying the modifier is the game
     * seam's business, under the setting - witnessed in-game, not here. */
    act_clear(); reset(); stub_menu = 0; poll(); act_clear();
    press(SDL_SCANCODE_LSHIFT, 0, 0);
    ck(act_on == 1 && (act_held & (1u << ACT_SPRINT)) && act_pulse[ACT_SPRINT] == 1
       && out_b == 0 && out_x == 0 && out_y == 0 && ch_walk == 0 && ch_strafe == 0,
       "Left Shift -> SPRINT held, one edge, no button/stick/walk", st());
    poll();
    ck((act_held & (1u << ACT_SPRINT)) && act_pulse[ACT_SPRINT] == 1,
       "Shift held across polls -> still held, ONE edge (a level)", st());
    act_clear();
    stub_keys[SDL_SCANCODE_W] = 1; poll();
    ck((act_held & (1u << ACT_SPRINT)) && ch_walk == 70 && ch_strafe == 0 && act_pulse[ACT_SPRINT] == 0,
       "W + Shift -> walk=70 unchanged, SPRINT held, no new edge", st());
    stub_keys[SDL_SCANCODE_D] = 1; poll();
    ck((act_held & (1u << ACT_SPRINT)) && ch_walk == 70 && ch_strafe == 70,
       "W + D + Shift -> walk=70 strafe=70 unchanged (no normalisation here)", st());
    stub_keys[SDL_SCANCODE_LSHIFT] = 0; poll();
    ck((act_held & (1u << ACT_SPRINT)) == 0 && act_pulse[ACT_SPRINT] == 0 && ch_walk == 70 && ch_strafe == 70,
       "Shift released -> SPRINT not held, no edge, channels unchanged", st());
    act_clear(); reset();
    stub_keys[SDL_SCANCODE_RSHIFT] = 1; poll();
    ck((act_held & (1u << ACT_SPRINT)) == 0, "Right Shift is NOT bound", st());
    act_clear(); reset(); stub_menu = 1;
    stub_keys[SDL_SCANCODE_LSHIFT] = 1; poll();
    ck(act_on == 0 && out_b == 0 && out_x == 0 && out_y == 0,
       "Shift in a menu -> channels off, no button, no stick (no menu leakage)", st());
    reset(); stub_menu = 2; stub_keys[SDL_SCANCODE_LSHIFT] = 1; poll();
    ck(act_on == 0 && out_b == 0, "Shift in the front end -> channels off, no button", st());
    reset(); stub_menu = 0; poll(); act_clear();

    printf("\n== BINDINGS (#46): the compiled default table ==\n");
    {
        /* Every default, read back through the registry as tokens - the one
         * table the input path, the parser and both editors share. A row =
         * action, kbm 1, kbm 2, pad 1, pad 2. The pad half is the DEFAULT
         * button layout, amended 2026-09-21 by the owner's bumper decision:
         * RT and RB fire, LT and LB aim, and the weapon cycle and the zoom
         * are the d-pad's alone (left / right cycle, up / down zoom); Y and
         * RS unbound. */
        static const struct { int action; const char *k1, *k2, *p1, *p2; } want[] = {
            { SL_ACT_MOVE_FORWARD,    "key:W",      "none",       "none",   "none"           },
            { SL_ACT_MOVE_BACK,       "key:S",      "none",       "none",   "none"           },
            { SL_ACT_STRAFE_LEFT,     "key:A",      "none",       "none",   "none"           },
            { SL_ACT_STRAFE_RIGHT,    "key:D",      "none",       "none",   "none"           },
            { SL_ACT_FIRE,            "mouse:LEFT", "key:F",      "pad:RT", "pad:RB"         },
            { SL_ACT_AIM,             "mouse:RIGHT","key:Q",      "pad:LT", "pad:LB"         },
            { SL_ACT_INTERACT,        "key:E",      "none",       "pad:A",  "none"           },
            { SL_ACT_RELOAD,          "key:R",      "none",       "pad:X",  "none"           },
            { SL_ACT_CROUCH,          "key:LCTRL",  "none",       "pad:B",  "none"           },
            { SL_ACT_SPRINT,          "key:LSHIFT", "none",       "pad:LS", "none"           },
            /* 2026-09-21: the weapon cycle and the zoom are the D-PAD's
             * alone on the pad - the bumpers mirror the triggers. */
            { SL_ACT_WEAPON_PREVIOUS, "key:1",      "wheel:UP",   "pad:DPAD_LEFT",  "none" },
            { SL_ACT_WEAPON_NEXT,     "key:2",      "wheel:DOWN", "pad:DPAD_RIGHT", "none" },
            { SL_ACT_ZOOM_IN,         "wheel:UP",   "none",       "pad:DPAD_UP",    "none" },
            { SL_ACT_ZOOM_OUT,        "wheel:DOWN", "none",       "pad:DPAD_DOWN",  "none" },
            /* #47: the texture-set cycle. F5 on the keyboard; on the pad,
             * BACK - the button that used to mark, which is why no pad slot
             * anywhere in this table reads "pad:BACK" for anything else. */
            { SL_ACT_TEXTURE_CYCLE,   "key:F5",     "none",       "pad:BACK",       "none" },
        };
        int ok = 1, j;
        char t[4][32], line[160];
        sl_bindings_reload();
        for (j = 0; j < (int) (sizeof want / sizeof want[0]); j++) {
            sl_bindings_source_token(sl_bindings_get(want[j].action, 0, 0), t[0], 32);
            sl_bindings_source_token(sl_bindings_get(want[j].action, 0, 1), t[1], 32);
            sl_bindings_source_token(sl_bindings_get(want[j].action, 1, 0), t[2], 32);
            sl_bindings_source_token(sl_bindings_get(want[j].action, 1, 1), t[3], 32);
            if (strcmp(t[0], want[j].k1) || strcmp(t[1], want[j].k2)
                || strcmp(t[2], want[j].p1) || strcmp(t[3], want[j].p2)) {
                ok = 0;
                snprintf(line, sizeof line, "%s: %s %s %s %s", sl_bindings_action_token(want[j].action),
                         t[0], t[1], t[2], t[3]);
                ck(0, "default row", line);
            }
        }
        ck(ok, "all 15 default rows read back as expected (15 actions x 4 slots)", "");
        ck(SL_ACT_COUNT == 15 && sl_bedit_action_count() == 15,
           "15 canonical actions; the editor lists all of them", "");
        for (j = 0; j < SL_ACT_COUNT; j++)
            if (sl_bindings_is_default(j, 0, 0) == 0 || sl_bindings_is_default(j, 1, 1) == 0) ok = 0;
        ck(ok, "every slot reports is_default with no overrides", "");
    }

    printf("\n== TEXTURE SET CYCLE (#47): F5, the pad's BACK, and the mark ==\n");
    {
        sl_bind_source s;
        char t[32];

        reset(); stub_menu = 0; poll(); act_clear(); tex_cycles = 0;

        /* The keyboard default. ONE cycle per press, however long it is held
         * - the same edge rule every other action gets, and the reason this
         * is a registry action instead of a private `static int held`. */
        stub_keys[SDL_SCANCODE_F5] = 1; poll();
        ck(tex_cycles == 1, "F5 pressed -> one texture-set cycle", "");
        poll(); poll();
        ck(tex_cycles == 1, "F5 HELD -> still one (no repeat)", "");
        stub_keys[SDL_SCANCODE_F5] = 0; poll();
        stub_keys[SDL_SCANCODE_F5] = 1; poll();
        ck(tex_cycles == 2, "F5 released and pressed again -> a second cycle", "");
        stub_keys[SDL_SCANCODE_F5] = 0; poll();

        /* The pad default: BACK - "View" / "Create" / "Select". The pad was
         * detached by the sections above, so attach one first. */
        pad_attach(SDL_CONTROLLER_TYPE_XBOX360);
        tex_cycles = 0;
        stub_padb[SDL_CONTROLLER_BUTTON_BACK] = 1; poll();
        ck(tex_cycles == 1, "pad BACK (View / Create / Select) -> one cycle", "");
        stub_padb[SDL_CONTROLLER_BUTTON_BACK] = 0; poll();
        ck(held_after_button(SDL_CONTROLLER_BUTTON_BACK) == (1u << ACT_TEXCYCLE),
           "DEFAULT: BACK -> TEXTURE SET and nothing else", "");

        /* A MENU OWNS INPUT: the press must not land, and it must not land
         * on the frame the menu closes either (the raw level memory has
         * already seen the button down, so no edge is manufactured). */
        tex_cycles = 0; stub_menu = 1;              /* the watch */
        stub_keys[SDL_SCANCODE_F5] = 1; poll(); poll();
        ck(tex_cycles == 0, "F5 while the WATCH owns input -> no cycle", "");
        stub_menu = 2; poll();
        ck(tex_cycles == 0, "F5 in the FRONT END -> no cycle", "");
        stub_menu = 0; poll(); poll();
        ck(tex_cycles == 0, "... and NOT on the frame the menu closes with F5 still down", "");
        stub_keys[SDL_SCANCODE_F5] = 0; poll();
        stub_keys[SDL_SCANCODE_F5] = 1; poll();
        ck(tex_cycles == 1, "a fresh press after the menu closes does cycle", "");
        stub_keys[SDL_SCANCODE_F5] = 0; poll(); act_clear();

        /* Re-bindable like any other row. */
        tex_cycles = 0;
        {   int stolen = -1;
            ck(sl_bindings_source_parse("key:F6", &s)
               && sl_bindings_set(SL_ACT_TEXTURE_CYCLE, 0, 0, &s, &stolen),
               "TEXTURE SET rebinds to F6 through the editor's own setter", "");
        }
        stub_keys[SDL_SCANCODE_F6] = 1; poll();
        ck(tex_cycles == 1, "F6 now cycles", "");
        stub_keys[SDL_SCANCODE_F6] = 0; poll();
        stub_keys[SDL_SCANCODE_F5] = 1; poll();
        ck(tex_cycles == 1, "... and F5 no longer does", "");
        stub_keys[SDL_SCANCODE_F5] = 0; poll();
        sl_bindings_reset_defaults();
        sl_bindings_source_token(sl_bindings_get(SL_ACT_TEXTURE_CYCLE, 0, 0), t, 32);
        ck(strcmp(t, "key:F5") == 0, "reset to defaults puts F5 back", t);

        /* THE MARK. The pad's BACK no longer marks (owner, #47: "We wont
         * need to mark with controller"), and no preset binds MARK on the
         * pad because MARK was never a registry action at all - it is F9 /
         * F8, deliberately OUTSIDE the bindable key table so nothing can
         * steal them. Asserted rather than assumed: if either key ever
         * became bindable, a player could lose the mark by rebinding. */
        ck(!sl_bindings_source_parse("key:F9", &s),
           "F9 (mark) is not a bindable source - the keyboard mark is untouched", "");
        ck(!sl_bindings_source_parse("key:F8", &s),
           "F8 (full mark) is not a bindable source either", "");
        {   int a, d, sl, back = 0;
            char tok[32];
            for (a = 0; a < SL_ACT_COUNT; a++)
                for (d = 0; d < SL_BIND_DEVICES; d++)
                    for (sl = 0; sl < SL_BIND_SLOTS; sl++) {
                        sl_bindings_source_token(sl_bindings_get(a, d, sl), tok, 32);
                        if (strcmp(tok, "pad:BACK") == 0) back++;
                    }
            ck(back == 1, "pad:BACK appears in the defaults exactly once - TEXTURE SET", "");
        }
        {   int p, a, sl, bad = 0, seen = 0;
            for (p = 0; p < SL_BUTTON_LAYOUT_PRESETS; p++)
                for (a = 0; a < SL_ACT_COUNT; a++)
                    for (sl = 0; sl < SL_BIND_SLOTS; sl++) {
                        const sl_bind_source *q = sl_bindings_layout_source(p, a, sl);
                        if (q != NULL && q->kind == SL_SRC_PAD_BUTTON
                            && q->code == SDL_CONTROLLER_BUTTON_BACK) {
                            seen++;
                            if (a != SL_ACT_TEXTURE_CYCLE) bad++;
                        }
                    }
            ck(bad == 0 && seen == SL_BUTTON_LAYOUT_PRESETS,
               "every preset binds BACK, and only to TEXTURE SET", "");
        }
        /* Leave the harness as this section found it: no pad, defaults. */
        stub_pad_present = 0; sl_input_live_device_change();
        reset(); poll(); act_clear(); tex_cycles = 0;
    }

    printf("\n== BINDINGS (#46): tokens and names ==\n");
    {
        sl_bind_source s;
        char b[32];
        ck(sl_bindings_source_parse("key:LSHIFT", &s) && s.kind == SL_SRC_KEY && s.code == SDL_SCANCODE_LSHIFT
           && !strcmp(sl_bindings_source_name(&s, b, 32), "LEFT SHIFT"), "key:LSHIFT -> scancode, LEFT SHIFT", b);
        ck(sl_bindings_source_parse("key:UP", &s) && s.code == SDL_SCANCODE_UP
           && !strcmp(sl_bindings_source_name(&s, b, 32), "UP ARROW"), "key:UP -> UP ARROW", b);
        ck(sl_bindings_source_parse("key:CAPSLOCK", &s) && s.code == SDL_SCANCODE_CAPSLOCK
           && !strcmp(sl_bindings_source_name(&s, b, 32), "CAPS LOCK"), "key:CAPSLOCK -> CAPS LOCK", b);
        ck(sl_bindings_source_parse("mouse:X1", &s) && s.kind == SL_SRC_MOUSE_BUTTON && s.code == SDL_BUTTON_X1
           && !strcmp(sl_bindings_source_name(&s, b, 32), "MOUSE 4"), "mouse:X1 -> button 4, MOUSE 4", b);
        ck(sl_bindings_source_parse("wheel:DOWN", &s) && s.kind == SL_SRC_WHEEL && s.dir == -1
           && !strcmp(sl_bindings_source_name(&s, b, 32), "WHEEL DOWN"), "wheel:DOWN -> dir -1, WHEEL DOWN", b);
        ck(sl_bindings_source_parse("pad:A", &s) && s.kind == SL_SRC_PAD_BUTTON && s.code == SDL_CONTROLLER_BUTTON_A
           && !strcmp(sl_bindings_source_name(&s, b, 32), "PAD A"), "pad:A -> button, PAD A", b);
        ck(sl_bindings_source_parse("pad:RT", &s) && s.kind == SL_SRC_PAD_AXIS && s.code == SDL_CONTROLLER_AXIS_TRIGGERRIGHT
           && s.dir == 1 && !strcmp(sl_bindings_source_name(&s, b, 32), "PAD RT"), "pad:RT -> trigger axis half, PAD RT", b);
        ck(sl_bindings_source_parse("none", &s) && s.kind == SL_SRC_NONE
           && !strcmp(sl_bindings_source_name(&s, b, 32), "---"), "none -> empty slot, ---", b);
        ck(!sl_bindings_source_parse("key:ESCAPE", &s), "key:ESCAPE is not a bindable token", "");
        ck(!sl_bindings_source_parse("key:", &s) && !sl_bindings_source_parse("keyboard:W", &s)
           && !sl_bindings_source_parse("pad:LEFTX", &s) && !sl_bindings_source_parse("42", &s),
           "malformed tokens (empty, unknown kind, a stick axis, a raw number) refused", "");
        s.kind = SL_SRC_KEY; s.code = SDL_SCANCODE_KP_7; s.dir = 0;
        ck(!strcmp(sl_bindings_source_token(&s, b, 32), "key:KP_7"), "scancode -> key:KP_7 (round trip)", b);
        s.kind = SL_SRC_KEY; s.code = SDL_SCANCODE_TAB;
        ck(!strcmp(sl_bindings_source_token(&s, b, 32), "none") && !sl_bindings_source_capturable(&s),
           "Tab is not capturable and formats as none", b);
        ck(sl_bindings_action_from_token("strafe_left") == SL_ACT_STRAFE_LEFT
           && !strcmp(sl_bindings_action_label(SL_ACT_WEAPON_PREVIOUS), "PREVIOUS WEAPON"),
           "action tokens and labels", "");
    }

    printf("\n== BINDINGS (#46): slots, conflicts, contexts ==\n");
    {
        sl_bind_source s;
        int from = -1;
        char b[32];
        sl_bindings_reload();
        /* SECONDARY, empty slot, steal in the same context. */
        sl_bindings_source_parse("key:F", &s);
        ck(sl_bindings_set(SL_ACT_INTERACT, 0, 0, &s, &from) == 1 && from == SL_ACT_FIRE,
           "INTERACT primary = F steals F from FIRE", "");
        ck(sl_bindings_get(SL_ACT_FIRE, 0, 1)->kind == SL_SRC_NONE, "FIRE's secondary is now EMPTY", "");
        ck(sl_bindings_get(SL_ACT_FIRE, 0, 0)->kind == SL_SRC_MOUSE_BUTTON, "FIRE's primary (mouse 1) untouched", "");
        sl_bindings_source_parse("key:T", &s);
        ck(sl_bindings_set(SL_ACT_RELOAD, 0, 1, &s, &from) == 1 && from == -1,
           "RELOAD secondary = T (a free key steals nothing)", "");
        ck(!strcmp(sl_bindings_source_token(sl_bindings_get(SL_ACT_RELOAD, 0, 0), b, 32), "key:R"),
           "RELOAD's primary still R (two slots, independent)", b);
        /* The same source may not sit in both slots of one action. */
        sl_bindings_source_parse("key:R", &s);
        sl_bindings_set(SL_ACT_RELOAD, 0, 1, &s, &from);
        ck(sl_bindings_get(SL_ACT_RELOAD, 0, 0)->kind == SL_SRC_NONE
           && sl_bindings_get(SL_ACT_RELOAD, 0, 1)->code == SDL_SCANCODE_R,
           "R into RELOAD's secondary empties its own primary", "");
        /* Contexts: wheel up on ZOOM IN (scoped) and PREVIOUS WEAPON (play)
         * coexist - the accepted default - and stay so after a re-set. */
        sl_bindings_reload();
        sl_bindings_source_parse("wheel:UP", &s);
        ck(sl_bindings_set(SL_ACT_ZOOM_IN, 0, 0, &s, &from) == 0,
           "wheel up already ZOOM IN's primary: no change", "");
        ck(sl_bindings_get(SL_ACT_WEAPON_PREVIOUS, 0, 1)->kind == SL_SRC_WHEEL,
           "PREVIOUS WEAPON keeps wheel up (different context, legal duplicate)", "");
        /* Round 6: a wheel row on an action outside the two pairs has NO
         * context (sl_bindings_source_ctx), so it conflicts with every use
         * of that notch - both PREVIOUS WEAPON (play) and ZOOM IN (scoped)
         * lose it. Until round 6 the INTERACT row counted as "play" and
         * ZOOM IN kept the notch; the one wheel semantic the shared rule
         * moved, for a custom binding only (the default table is untouched). */
        ck(sl_bindings_set(SL_ACT_INTERACT, 0, 1, &s, &from) == 1 && from >= 0
           && sl_bindings_get(SL_ACT_WEAPON_PREVIOUS, 0, 1)->kind == SL_SRC_NONE
           && sl_bindings_get(SL_ACT_ZOOM_IN, 0, 0)->kind == SL_SRC_NONE,
           "wheel up onto INTERACT (no context) steals it from PREVIOUS WEAPON AND ZOOM IN", "");
        sl_bindings_reload();
        act_clear(); reset(); stub_menu = 0; stub_scoped = 1; poll(); act_clear();
        sl_bindings_source_parse("wheel:UP", &s);
        sl_bindings_set(SL_ACT_INTERACT, 0, 1, &s, &from);
        sl_input_live_wheel(1); poll();
        ck(act_pulse[ACT_INTERACT] == 1 && act_pulse[ACT_ZOOMIN] == 0,
           "... and that notch interacts inside the scope too (a wheel row with no context)", st());
        stub_scoped = 0; act_clear();
        sl_bindings_reload();
        /* Wrong device class for the slot, and an uncapturable key, refused. */
        sl_bindings_source_parse("pad:A", &s);
        ck(sl_bindings_set(SL_ACT_INTERACT, 0, 0, &s, &from) == 0, "a pad button into a KBM slot is refused", "");
        s.kind = SL_SRC_KEY; s.code = SDL_SCANCODE_ESCAPE; s.dir = 0;
        ck(sl_bindings_set(SL_ACT_INTERACT, 0, 0, &s, &from) == 0, "Escape into a slot is refused", "");
        ck(sl_bindings_set(SL_ACT_INTERACT, 0, 0, NULL, &from) == 1
           && sl_bindings_get(SL_ACT_INTERACT, 0, 0)->kind == SL_SRC_NONE, "NULL empties a slot", "");
        sl_bindings_reset_defaults();
        ck(sl_bindings_is_default(SL_ACT_INTERACT, 0, 0) && sl_bindings_is_default(SL_ACT_INTERACT, 0, 1)
           && sl_bindings_is_default(SL_ACT_WEAPON_PREVIOUS, 0, 1), "reset restores every default", "");
    }

    printf("\n== BINDINGS (#46): evaluation after a rebind - the consumed channels ==\n");
    {
        sl_bind_source s;
        int from;
        sl_bindings_reload();
        act_clear(); reset(); stub_menu = 0; poll(); act_clear();
        press(SDL_SCANCODE_W, 0, 0);
        ck(ch_walk == 70 && (act_held & (1u << ACT_MOVE_F)), "default: W -> walk=70, MOVE_FORWARD held", st());
        sl_bindings_source_parse("key:UP", &s);
        sl_bindings_set(SL_ACT_MOVE_FORWARD, 0, 0, &s, &from);
        act_clear(); press(SDL_SCANCODE_W, 0, 0);
        ck(ch_walk == 0 && (act_held & (1u << ACT_MOVE_F)) == 0, "MOVE FORWARD = UP: W alone -> NO walk", st());
        act_clear(); press(SDL_SCANCODE_UP, 0, 0);
        /* The arrows are still the d-pad in play (unchanged): a bound arrow
         * walks through the channel AND raises its d-pad bit as before. */
        ck(ch_walk == 70 && (act_held & (1u << ACT_MOVE_F)) && (out_b & SL_BTN_Z) == 0 && out_b == SL_BTN_DUP,
           "UP ARROW -> walk=70, MOVE_FORWARD held (d-pad up as ever)", st());
        act_clear(); reset(); stub_menu = 1; stub_keys[SDL_SCANCODE_W] = 1; poll();
        ck(out_y == 70, "in the WATCH, W still deflects the stick (menus never rebind)", st());
        reset(); stub_menu = 2; stub_keys[SDL_SCANCODE_W] = 1; poll();
        ck(out_y == 80, "in the FRONT END, W still deflects the stick", st());
        reset(); stub_menu = 0; poll();
        /* Fire and aim through the registry. */
        act_clear(); press(SDL_SCANCODE_F, 0, 0);
        ck((out_b & SL_BTN_Z) && (act_held & (1u << ACT_FIRE)), "default: F -> Z (FIRE held)", st());
        sl_bindings_source_parse("key:F", &s);
        sl_bindings_set(SL_ACT_INTERACT, 0, 0, &s, &from);
        act_clear(); press(SDL_SCANCODE_F, 0, 0);
        ck((out_b & SL_BTN_Z) == 0 && (act_held & (1u << ACT_INTERACT)) && act_pulse[ACT_INTERACT] == 1
           && act_pulse[ACT_RELOAD] == 0,
           "INTERACT = F: F -> INTERACT edge, NO Z, no RELOAD", st());
        act_clear(); press(SDL_SCANCODE_E, 0, 0);
        ck((act_held & (1u << ACT_INTERACT)) == 0 && act_pulse[ACT_INTERACT] == 0, "E no longer interacts", st());
        sl_bindings_source_parse("mouse:X1", &s);
        sl_bindings_set(SL_ACT_FIRE, 0, 0, &s, &from);
        reset(); stub_menu = 0; stub_buttons = SDL_BUTTON(SDL_BUTTON_LEFT); poll();
        ck((out_b & SL_BTN_Z) == 0, "FIRE = MOUSE 4: the left button no longer fires", st());
        reset(); stub_buttons = SDL_BUTTON(SDL_BUTTON_X1); poll();
        ck((out_b & SL_BTN_Z) != 0, "MOUSE 4 fires", st());
        reset(); stub_buttons = SDL_BUTTON(SDL_BUTTON_RIGHT); poll();
        ck((out_b & SL_BTN_R) != 0, "the right button still aims", st());
        reset(); stub_buttons = SDL_BUTTON(SDL_BUTTON_MIDDLE); poll();
        ck((out_b & SL_BTN_R) == 0, "the middle button is no longer a default aim alias", st());
        /* A mouse button in a menu never reaches the actions (the #40 click rules). */
        reset(); stub_menu = 1; stub_buttons = SDL_BUTTON(SDL_BUTTON_X1); poll();
        ck((out_b & SL_BTN_Z) == 0 && act_on == 0, "a bound mouse button in the watch -> no Z", st());
        reset(); stub_menu = 0; poll();
        sl_bindings_reset_defaults();
        act_clear(); press(SDL_SCANCODE_W, 0, 0);
        ck(ch_walk == 70, "after reset: W walks again", st());
        reset(); stub_menu = 0; stub_buttons = SDL_BUTTON(SDL_BUTTON_LEFT); poll();
        ck((out_b & SL_BTN_Z) != 0, "after reset: the left button fires again", st());
        reset(); stub_buttons = 0; poll(); act_clear();
    }

    printf("\n== BINDINGS (#46): persistence through the store ==\n");
    {
        char *path = g_scratch_ini, t[32];
        static char env[600];
        const char *tmpdir = getenv("TEMP");
        sl_bind_source s;
        int from;
        if (tmpdir == NULL || tmpdir[0] == '\0') tmpdir = ".";
        snprintf(g_scratch_ini, sizeof g_scratch_ini, "%s\\sl_inputtest_bindings.ini", tmpdir);
        remove(path);
        snprintf(env, sizeof env, "SL_CONFIG=%s", path);
        putenv(env);
        sl_settings_init();
        sl_bindings_reload();
        ck(sl_settings_active() && sl_settings_ext_count() == 0, "store active on a scratch file, no bind lines", path);
        sl_bindings_source_parse("key:T", &s);
        sl_bindings_set(SL_ACT_RELOAD, 0, 0, &s, &from);
        sl_bindings_source_parse("key:F", &s);
        sl_bindings_set(SL_ACT_INTERACT, 0, 0, &s, &from);      /* steals from FIRE secondary */
        ck(sl_settings_ext_count() == 3
           && !strcmp(sl_settings_ext_get("bind.reload.kbm.1"), "key:T")
           && !strcmp(sl_settings_ext_get("bind.interact.kbm.1"), "key:F")
           && !strcmp(sl_settings_ext_get("bind.fire.kbm.2"), "none"),
           "three lines: reload=T, interact=F, fire secondary=none (the steal persisted)", "");
        /* Reload from the file: the registry comes back the same. */
        {
            FILE *f = fopen(path, "r"); char buf[2048]; size_t n = 0;
            if (f) { n = fread(buf, 1, sizeof buf - 1, f); fclose(f); }
            buf[n] = '\0';
            ck(strstr(buf, "bind.reload.kbm.1=key:T\n") != NULL && strstr(buf, "bind.fire.kbm.2=none\n") != NULL
               && strstr(buf, "sprint_enabled=0\n") != NULL, "the file carries the scalars and the bind lines", "");
        }
        sl_bindings_reload();       /* memory only: the table from the store's lines */
        ck(!strcmp(sl_bindings_source_token(sl_bindings_get(SL_ACT_RELOAD, 0, 0), t, 32), "key:T")
           && sl_bindings_get(SL_ACT_FIRE, 0, 1)->kind == SL_SRC_NONE
           && !strcmp(sl_bindings_source_token(sl_bindings_get(SL_ACT_INTERACT, 0, 0), t, 32), "key:F"),
           "reload from the store reproduces the table", "");
        /* A malformed override in the file: THAT slot falls back, the rest load. */
        {
            FILE *f = fopen(path, "w");
            if (f) {
                fputs("version=1\nbind.reload.kbm.1=key:NOSUCHKEY\nbind.crouch.kbm.1=pad:A\n"
                      "bind.sprint.kbm.1=key:CAPSLOCK\nbind.no_such_action.kbm.1=key:Z\n", f);
                fclose(f);
            }
        }
        /* The store is a once-only singleton in a process (its file parser
         * is asserted by settingstest.ps1), so the fallback is asserted on
         * the REGISTRY side by feeding it the same lines through the ext
         * API the parser fills. */
        sl_settings_batch_begin();
        sl_settings_ext_remove_prefix("bind.");
        sl_settings_ext_set("bind.reload.kbm.1", "key:NOSUCHKEY");
        sl_settings_ext_set("bind.crouch.kbm.1", "pad:A");
        sl_settings_ext_set("bind.sprint.kbm.1", "key:CAPSLOCK");
        sl_settings_ext_set("bind.no_such_action.kbm.1", "key:Z");
        sl_settings_batch_end();
        sl_bindings_reload();
        ck(!strcmp(sl_bindings_source_token(sl_bindings_get(SL_ACT_RELOAD, 0, 0), t, 32), "key:R"),
           "malformed token -> that slot at its default (R)", t);
        ck(!strcmp(sl_bindings_source_token(sl_bindings_get(SL_ACT_CROUCH, 0, 0), t, 32), "key:LCTRL"),
           "a pad source in a kbm slot -> that slot at its default", t);
        ck(!strcmp(sl_bindings_source_token(sl_bindings_get(SL_ACT_SPRINT, 0, 0), t, 32), "key:CAPSLOCK"),
           "the well-formed override beside them loads (SPRINT = CAPS LOCK)", t);
        ck(sl_settings_ext_get("bind.no_such_action.kbm.1") != NULL, "an unknown action's line is kept, ignored", "");
        sl_bindings_reset_defaults();
        ck(sl_settings_ext_count() == 0, "reset removes every bind line from the store", "");
        ck(sl_settings_get(SL_SET_SPRINT_ENABLED) == 0 && sl_settings_get(SL_SET_PAD_STICK_LAYOUT) == 0,
           "reset leaves the other settings alone", "");
    }

    printf("\n== BINDINGS (#46): capture ==\n");
    {
        int a, d, sl;
        char t[32];
        sl_bindings_reload();
        reset(); stub_menu = 2; stub_ptrmenu = 1; stub_clickadv = 1; poll();
        /* The confirm key that starts the capture is held at begin: ignored
         * until released. A source held at begin likewise. */
        stub_keys[SDL_SCANCODE_RETURN] = 1; stub_keys[SDL_SCANCODE_G] = 1;
        sl_bindings_capture_begin(SL_ACT_INTERACT, 0, 0);
        ck(sl_bindings_capture_active() && sl_bindings_capture_slot(&a, &d, &sl) && a == SL_ACT_INTERACT && sl == 0,
           "capture begins on INTERACT primary", "");
        poll();
        ck(sl_bindings_capture_active(), "a key held at begin (G) does not bind", "");
        ck(out_x == 0 && out_y == 0 && out_b == 0, "menu input is neutral while capturing", st());
        stub_keys[SDL_SCANCODE_G] = 0; poll();
        ck(sl_bindings_capture_active(), "still waiting after its release", "");
        stub_keys[SDL_SCANCODE_G] = 1; poll();
        ck(!sl_bindings_capture_active()
           && !strcmp(sl_bindings_source_token(sl_bindings_get(SL_ACT_INTERACT, 0, 0), t, 32), "key:G"),
           "a fresh press of G binds INTERACT = G", t);
        ck(sl_bindings_capture_blocking() && out_y == 0, "the captured key still held keeps menu input neutral", st());
        stub_keys[SDL_SCANCODE_G] = 0; stub_keys[SDL_SCANCODE_RETURN] = 0; poll();
        ck(!sl_bindings_capture_blocking(), "released: the block lifts", "");
        /* Escape cancels and never binds. */
        sl_bindings_capture_begin(SL_ACT_RELOAD, 0, 1);
        sl_input_live_escape(); poll();
        ck(!sl_bindings_capture_active() && sl_bindings_get(SL_ACT_RELOAD, 0, 1)->kind == SL_SRC_NONE
           && (out_b & (SL_BTN_B | SL_BTN_START)) == 0,
           "Escape cancels: slot unchanged, no B / START synthesised", st());
        /* The click that starts a capture is not the binding; a fresh click is. */
        stub_buttons = SDL_BUTTON(SDL_BUTTON_LEFT);
        sl_input_live_click(SDL_BUTTON_LEFT, 1);
        sl_bindings_capture_begin(SL_ACT_FIRE, 0, 0);
        poll();
        ck(sl_bindings_capture_active() && sl_bindings_get(SL_ACT_FIRE, 0, 0)->code == SDL_BUTTON_LEFT,
           "the starting click (held) is not captured", "");
        stub_buttons = 0; sl_input_live_release(SDL_BUTTON_LEFT); poll();
        stub_buttons = SDL_BUTTON(SDL_BUTTON_X2); sl_input_live_click(SDL_BUTTON_X2, 1); poll();
        ck(!sl_bindings_capture_active()
           && !strcmp(sl_bindings_source_token(sl_bindings_get(SL_ACT_FIRE, 0, 0), t, 32), "mouse:X2"),
           "a fresh MOUSE 5 press binds FIRE = MOUSE 5", t);
        ck((out_b & SL_BTN_A) == 0, "that click was not a menu confirm", st());
        stub_buttons = 0; sl_input_live_release(SDL_BUTTON_X2); poll();
        /* The wheel is a capture's answer, never a menu step meanwhile. */
        sl_bindings_capture_begin(SL_ACT_ZOOM_IN, 0, 1);
        sl_input_live_wheel(-1); poll();
        ck(!sl_bindings_capture_active() && out_y == 0
           && !strcmp(sl_bindings_source_token(sl_bindings_get(SL_ACT_ZOOM_IN, 0, 1), t, 32), "wheel:DOWN"),
           "a notch binds ZOOM IN secondary = WHEEL DOWN and does not step the menu", t);
        ck(sl_bindings_get(SL_ACT_WEAPON_NEXT, 0, 1)->kind == SL_SRC_WHEEL,
           "NEXT WEAPON keeps wheel down (scoped vs play: legal duplicate)", "");
        for (i = 0; i < 8; i++) poll();
        /* Backspace clears. Tab during a capture does not synthesise START. */
        sl_bindings_capture_begin(SL_ACT_CROUCH, 0, 0);
        sl_input_live_start(); poll();
        ck(sl_bindings_capture_active() && (out_b & SL_BTN_START) == 0, "Tab during a capture is ignored", st());
        stub_keys[SDL_SCANCODE_BACKSPACE] = 1; poll();
        ck(!sl_bindings_capture_active() && sl_bindings_get(SL_ACT_CROUCH, 0, 0)->kind == SL_SRC_NONE,
           "Backspace empties the slot", "");
        stub_keys[SDL_SCANCODE_BACKSPACE] = 0; poll();
        /* Only sources of the slot's device class bind. */
        sl_bindings_capture_begin(SL_ACT_INTERACT, 1, 0);
        stub_keys[SDL_SCANCODE_H] = 1; poll();
        ck(sl_bindings_capture_active(), "a key does not bind a CONTROLLER slot", "");
        stub_keys[SDL_SCANCODE_H] = 0; sl_bindings_capture_cancel(); poll();
        /* Focus loss abandons a capture. */
        sl_bindings_capture_begin(SL_ACT_INTERACT, 0, 0);
        sl_input_live_focus(0); sl_input_live_focus(1);
        ck(!sl_bindings_capture_active(), "focus loss cancels a capture", "");
        sl_bindings_reset_defaults();
        /* The focus loss also disarmed click-to-capture (its contract); the
         * sections after this one assume the pointer is held, as the first
         * section left it - so take it again the way a player would. */
        reset(); stub_menu = 0; stub_ptrmenu = 0; stub_clickadv = 0; poll();
        sl_input_live_click(SDL_BUTTON_LEFT, 1);
        stub_buttons = 0; sl_input_live_release(SDL_BUTTON_LEFT); poll();
        ck(stub_relmode == 1, "the pointer is held again for the sections that follow", st());
        act_clear();
    }

    printf("\n== BINDINGS (#46): the shared editor model ==\n");
    {
        char t[48];
        sl_bind_source s;
        int from, row = -1, j;
        sl_bindings_reload();
        sl_bedit_open(SL_BEDIT_SHELL_WATCH);
        ck(sl_bedit_is_open() && sl_bedit_shell() == SL_BEDIT_SHELL_WATCH && sl_bedit_device() == 0,
           "opened for the watch, KEYBOARD/MOUSE tab", "");
        for (j = 0; j < sl_bedit_action_count(); j++)
            if (sl_bedit_action_at(j) == SL_ACT_RELOAD) row = j;
        ck(row >= 0 && !strcmp(sl_bedit_action_label(row), "RELOAD"), "RELOAD is in the editor's list", "");
        sl_bedit_slot_text(row, 0, t, 48);
        ck(!strcmp(t, "R"), "RELOAD primary reads R", t);
        sl_bindings_source_parse("key:T", &s);
        sl_bindings_set(SL_ACT_RELOAD, 0, 0, &s, &from);     /* as the OTHER shell would */
        sl_bedit_slot_text(row, 0, t, 48);
        ck(!strcmp(t, "T"), "a change through the registry shows in the model at once (no sync)", t);
        sl_bedit_slot_text(row, 1, t, 48);
        ck(!strcmp(t, "---"), "the empty secondary reads ---", t);
        sl_bedit_activate_slot(row, 1);
        sl_bedit_slot_text(row, 1, t, 48);
        ck(sl_bedit_capturing() && sl_bedit_slot_capturing(row, 1) && !strcmp(t, "PRESS A KEY"),
           "activating a slot captures and the slot reads PRESS A KEY", t);
        sl_bedit_activate_slot(row, 1);
        ck(!sl_bedit_capturing(), "activating the waiting slot again cancels", "");
        sl_bedit_set_device(1);
        sl_bedit_slot_text(row, 0, t, 48);
        ck(!strcmp(t, "PAD X"), "RELOAD's pad primary reads PAD X (no pad attached: the neutral name)", t);
        sl_bedit_reset();
        sl_bedit_slot_text(row, 0, t, 48);
        ck(!strcmp(sl_bedit_message(), "DEFAULTS RESTORED") && sl_bindings_layout_get() == SL_BUTTON_LAYOUT_DEFAULT,
           "reset: message, and the BUTTON LAYOUT reads DEFAULT", sl_bedit_message());
        sl_bedit_close();
        ck(!sl_bedit_is_open(), "closed", "");
    }

    printf("\n== BINDINGS (#46 / #63): the controller through the registry (synthetic pad) ==\n");
    {
        sl_bind_source s;
        int from;
        char t[32];
        sl_bindings_reload();
        stub_pad_present = 1;
        sl_input_live_device_change();
        memset(stub_padb, 0, sizeof stub_padb); memset(stub_pada, 0, sizeof stub_pada);
        reset(); stub_menu = 0; poll(); act_clear();
        /* The registry decides every button; there is no fixed map any more
         * (the ORIGINAL identity cases that stood here until 2026-09-20 -
         * RB -> Z by the fixed map, A -> N64 B, the style path - asserted a
         * path that no longer exists and went with it, #63). */
        act_clear(); stub_padb[SDL_CONTROLLER_BUTTON_A] = 1; poll();
        ck((out_b & SL_BTN_B) == 0 && (act_held & (1u << ACT_INTERACT)) && act_pulse[ACT_INTERACT] == 1,
           "A -> INTERACT (no N64 B)", st());
        stub_padb[SDL_CONTROLLER_BUTTON_A] = 0; act_clear();
        stub_padb[SDL_CONTROLLER_BUTTON_RIGHTSHOULDER] = 1; poll();
        ck((out_b & SL_BTN_Z) != 0 && (act_held & (1u << ACT_FIRE)),
           "RB -> FIRE -> Z (the DEFAULT layout since 2026-09-21: the bumpers mirror the triggers)", st());
        stub_padb[SDL_CONTROLLER_BUTTON_RIGHTSHOULDER] = 0; act_clear();
        stub_pada[SDL_CONTROLLER_AXIS_TRIGGERLEFT] = 20000; poll();
        ck((out_b & SL_BTN_R) != 0 && (act_held & (1u << ACT_AIM)), "LT (digital half) -> AIM -> R", st());
        stub_pada[SDL_CONTROLLER_AXIS_TRIGGERLEFT] = 0;
        stub_padb[SDL_CONTROLLER_BUTTON_X] = 1; poll();
        ck((out_b & SL_BTN_A) == 0 && (act_held & (1u << ACT_RELOAD)), "X -> RELOAD, never an N64 A", st());
        stub_padb[SDL_CONTROLLER_BUTTON_X] = 0;
        /* Remap FIRE's pad primary to B: RT no longer fires, B does. */
        sl_bindings_source_parse("pad:B", &s);
        sl_bindings_set(SL_ACT_FIRE, 1, 0, &s, &from);
        ck(from == SL_ACT_CROUCH && sl_bindings_get(SL_ACT_CROUCH, 1, 0)->kind == SL_SRC_NONE,
           "pad B onto FIRE steals it from CROUCH", "");
        ck(sl_bindings_layout_get() == SL_BUTTON_LAYOUT_CUSTOM, "... and the BUTTON LAYOUT reads CUSTOM", "");
        stub_pada[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] = 20000; poll();
        ck((out_b & SL_BTN_Z) == 0, "RT no longer fires (slot taken by B)", st());
        stub_pada[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] = 0;
        stub_padb[SDL_CONTROLLER_BUTTON_B] = 1; poll();
        ck((out_b & SL_BTN_Z) != 0 && (act_held & (1u << ACT_CROUCH)) == 0, "B fires, no crouch", st());
        stub_padb[SDL_CONTROLLER_BUTTON_B] = 0;
        /* The sticks are the channels, whatever the buttons say. */
        stub_pada[SDL_CONTROLLER_AXIS_LEFTY] = -32000; poll();
        ck(ch_on == 1 && ch_walk > 60 && out_y == 0, "the left stick still walks (the channel, the N64 stick neutral)", st());
        stub_pada[SDL_CONTROLLER_AXIS_LEFTY] = 0;
        /* Trigger capture on a pad slot. */
        stub_menu = 2; stub_ptrmenu = 1; stub_clickadv = 1; poll();
        sl_bindings_capture_begin(SL_ACT_SPRINT, 1, 0);
        stub_pada[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] = 20000; poll();
        ck(!sl_bindings_capture_active()
           && !strcmp(sl_bindings_source_token(sl_bindings_get(SL_ACT_SPRINT, 1, 0), t, 32), "pad:RT"),
           "a trigger pull binds SPRINT pad primary = PAD RT", t);
        stub_pada[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] = 0; poll();
        sl_bindings_capture_begin(SL_ACT_SPRINT, 1, 1);
        stub_pada[SDL_CONTROLLER_AXIS_LEFTX] = 32000; poll();
        ck(sl_bindings_capture_active(), "a stick deflection never captures", "");
        stub_pada[SDL_CONTROLLER_AXIS_LEFTX] = 0; sl_bindings_capture_cancel();
        stub_menu = 0; poll(); act_clear();
        sl_bindings_reset_defaults();
        stub_pad_present = 0;
        sl_input_live_device_change();
        reset(); stub_menu = 0; poll(); act_clear();
        if (g_scratch_ini[0]) remove(g_scratch_ini);
    }

    printf("\n== menu navigation: the stick value the game reads ==\n");
    press(SDL_SCANCODE_W, 1, 0);
    ck(out_y == 80, "W in a menu -> stick_y = +80 (up)", st());
    press(SDL_SCANCODE_S, 1, 0);
    ck(out_y == -80, "S in a menu -> stick_y = -80 (down)", st());
    press(SDL_SCANCODE_UP, 1, 0);
    ck(out_y == 80, "Up in a menu -> stick_y = +80", st());
    press(SDL_SCANCODE_DOWN, 1, 0);
    ck(out_y == -80, "Down in a menu -> stick_y = -80", st());
    press(SDL_SCANCODE_A, 1, 0);
    ck(out_x == -80, "A in a menu -> stick_x = -80 (left)", st());
    press(SDL_SCANCODE_D, 1, 0);
    ck(out_x == 80, "D in a menu -> stick_x = +80 (right)", st());
    press(SDL_SCANCODE_LEFT, 1, 0);
    ck(out_x == -80, "Left in a menu -> stick_x = -80", st());
    press(SDL_SCANCODE_RIGHT, 1, 0);
    ck(out_x == 80, "Right in a menu -> stick_x = +80", st());

    printf("\n== wheel: one discrete step per notch, menus only ==\n");
    /* Three notches up. Count the RISING EDGES of a positive stick_y and
     * confirm the stick returns to centre between them - which is what the
     * watch menus require to re-arm (options.c:1458-1472). */
    reset(); stub_menu = 1;
    sl_input_live_wheel(3);
    steps_up = 0; saw_centre = 1;
    for (i = 0; i < 40; i++) {
        signed char prev = out_y;
        poll();
        if (out_y > 0 && prev <= 0) steps_up++;
        if (out_y == 0) saw_centre = 1;
    }
    ck(steps_up == 3, "3 notches up -> exactly 3 menu steps up", st());
    ck(saw_centre, "the stick returns to centre between notches", st());

    reset(); stub_menu = 1;
    sl_input_live_wheel(-2);
    steps_down = 0;
    for (i = 0; i < 40; i++) {
        signed char prev = out_y;
        poll();
        if (out_y < 0 && prev >= 0) steps_down++;
    }
    ck(steps_down == 2, "2 notches down -> exactly 2 menu steps down", st());

    /* A hard spin must not free-run the menu. */
    reset(); stub_menu = 1;
    sl_input_live_wheel(50);
    steps_up = 0;
    for (i = 0; i < 200; i++) {
        signed char prev = out_y;
        poll();
        if (out_y > 0 && prev <= 0) steps_up++;
    }
    ck(steps_up <= 4, "a spun wheel is capped, not banked", st());

    /* Notches spun during play are discarded, not delivered on the next menu. */
    reset(); stub_menu = 0;
    sl_input_live_wheel(3);
    for (i = 0; i < 5; i++) poll();
    stub_menu = 1;
    steps_up = 0;
    for (i = 0; i < 30; i++) {
        signed char prev = out_y;
        poll();
        if (out_y > 0 && prev <= 0) steps_up++;
    }
    ck(steps_up == 0, "notches spun outside a menu are discarded", st());
    reset(); stub_menu = 0; for (i = 0; i < 5; i++) poll();

    printf("\n== mouse motion is drained and ignored in menus ==\n");
    motion(50, 50, 1, 0);
    ck(out_x == 0 && out_y == 0 && ch_on == 0,
       "mouse motion moves nothing in a menu", st());
    motion(0, 0, 0, 0);
    ck(ch_turn == 0 && ch_pitch == 0,
       "and is not banked for the frame the menu closes", st());

    /* These are stated in SDL dx/dy, which is what `motion()` supplies, NOT in
     * physical directions - the two are related by g_dx_sign / g_dy_sign, and
     * that relation is the one link in the whole chain this machine cannot
     * measure without a hand on a real mouse.
     *
     * What IS measured, on the rendered image, is everything downstream:
     * positive analogTurn turns the camera RIGHT and positive analogPitch looks
     * DOWN. So these assertions pin the composition from SDL's numbers onward,
     * and the physical mapping is pinned separately by the two sign defaults
     * asserted below. */
    printf("\n== SDL dx/dy -> the four channels (gameplay) ==\n");
    motion(40, 0, 0, 0);
    ck(ch_on == 1 && ch_turn < 0, "SDL dx>0 -> negative analogTurn (dx_sign=-1)", st());
    motion(-40, 0, 0, 0);
    ck(ch_on == 1 && ch_turn > 0, "SDL dx<0 -> positive analogTurn (camera right)", st());
    motion(0, -40, 0, 0);
    ck(ch_on == 1 && ch_pitch > 0, "SDL dy<0 -> positive analogPitch (camera down)", st());
    motion(0, 40, 0, 0);
    ck(ch_on == 1 && ch_pitch < 0, "SDL dy>0 -> negative analogPitch (camera up)", st());

    /* The defaults themselves, asserted through behaviour rather than by
     * reading the variables, so a change to either is caught here.
     *
     * BOTH ARE -1, and both are owner-validated physically: plain play.sh with
     * no environment gave LEFT->LEFT, RIGHT->RIGHT, UP->UP, DOWN->DOWN on a
     * real mouse. This pair of assertions is the regression guard for that
     * result - if either default is ever changed back, these fail. */
    printf("\n== the physical-direction convention defaults (both -1) ==\n");
    motion(40, 40, 0, 0);
    ck(ch_turn < 0, "X inverted by default (SL_MOUSE_DX_SIGN=-1)", st());
    ck(ch_pitch < 0, "Y inverted by default (SL_MOUSE_DY_SIGN=-1)", st());

    printf("\n== WASD -> the four channels (must not regress) ==\n");
    press(SDL_SCANCODE_W, 0, 0);
    ck(ch_on == 1 && ch_walk > 0, "W -> positive analogWalk (forward)", st());
    press(SDL_SCANCODE_S, 0, 0);
    ck(ch_on == 1 && ch_walk < 0, "S -> negative analogWalk (back)", st());
    press(SDL_SCANCODE_A, 0, 0);
    ck(ch_on == 1 && ch_strafe < 0, "A -> negative analogStrafe (left)", st());
    press(SDL_SCANCODE_D, 0, 0);
    ck(ch_on == 1 && ch_strafe > 0, "D -> positive analogStrafe (right)", st());

    /* Aim mode hands the mouse to the N64 stick, because that is where the game
     * reads aim turn (bondview2.c:5268) and the crosshair (:6096) from while
     * aiming. The expected SIGNS here are not a guess and not the same as the
     * menu's:
     *
     *   stick_y > 60 while aiming raises speedVertaDown (:5279).
     *   bondviewCurrentPlayerUpdateSpeedVerta(+v) drives speedverta DOWNWARD
     *   (:4010-4030, it subtracts), which is the same direction positive
     *   analogPitch drives it through canNaturalPitch (:6031, speedverta =
     *   -analogPitch/70 squared-signed).
     *
     * Positive analogPitch was MEASURED to look DOWN (docs/decisions/
     * native-input-signs.md, re-measured 2026-09-01), so positive stick_y while
     * aiming looks down too, and mouse DOWN must produce it. This half is
     * therefore derivation anchored on a measurement, not a second measurement:
     * the aim path itself was not re-measured on the renderer. */
    printf("\n== aim mode: the mouse becomes the N64 stick ==\n");
    motion(40, 0, 0, 1);
    ck(ch_on == 0 && out_x < 0, "aiming, SDL dx>0 -> negative stick_x", st());
    motion(-40, 0, 0, 1);
    ck(ch_on == 0 && out_x > 0, "aiming, SDL dx<0 -> positive stick_x (camera right)", st());
    motion(0, -40, 0, 1);
    ck(ch_on == 0 && out_y > 0, "aiming, SDL dy<0 -> positive stick_y (camera down)", st());
    motion(0, 40, 0, 1);
    ck(ch_on == 0 && out_y < 0, "aiming, SDL dy>0 -> negative stick_y (camera up)", st());

    /* THE MOUSE OWNS ITS OWN PITCH.
     *
     * GoldenEye negates analogPitch, negates controlStickYRaw and swaps
     * speedVertaDown/Up when the player has selected UPRIGHT in Look Up/Down
     * (bondview2.c:4849 sets invertPitch to the NEGATION of the setting, :5579
     * acts on invertPitch == 0). Everything this layer emits passes through
     * that block, so the layer pre-negates to cancel it.
     *
     * The contract is therefore about the COMPOSITION, not about the emitted
     * number: model the game block here and assert that what the game finally
     * acts on is the same whichever way the option is set - and conventional.
     * Asserting the raw emitted value instead would lock in the compensation
     * without ever checking it cancels. */
    printf("\n== mouse pitch is conventional under BOTH Look Up/Down settings ==\n");
    {
        int eff_normal[2], eff_aim[2], up;
        for (up = 0; up <= 1; up++) {
            stub_upright = up;
            motion(0, -40, 0, 0);                /* physical DOWN, normal look */
            eff_normal[up] = up ? -ch_pitch : ch_pitch;
            motion(0, -40, 0, 1);                /* physical DOWN, aiming */
            eff_aim[up] = up ? -out_y : out_y;
        }
        stub_upright = 0;
        ck(eff_normal[0] > 0 && eff_normal[1] > 0,
           "physical DOWN -> pitch down, both Look settings (normal)", st());
        ck(eff_normal[0] == eff_normal[1],
           "the Look setting does not change mouse pitch (normal)", st());
        ck(eff_aim[0] > 0 && eff_aim[1] > 0,
           "physical DOWN -> pitch down, both Look settings (aiming)", st());
        ck(eff_aim[0] == eff_aim[1],
           "the Look setting does not change mouse pitch (aiming)", st());

        for (up = 0; up <= 1; up++) {
            stub_upright = up;
            motion(0, 40, 0, 0);                 /* physical UP, normal look */
            eff_normal[up] = up ? -ch_pitch : ch_pitch;
            motion(0, 40, 0, 1);                 /* physical UP, aiming */
            eff_aim[up] = up ? -out_y : out_y;
        }
        stub_upright = 0;
        ck(eff_normal[0] < 0 && eff_normal[1] < 0,
           "physical UP -> pitch up, both Look settings (normal)", st());
        ck(eff_aim[0] < 0 && eff_aim[1] < 0,
           "physical UP -> pitch up, both Look settings (aiming)", st());
    }

    /* ======================================================================
     * THE NATIVE MENU POINTER
     *
     * Everything asserted here is in the SHIPPING src/platform/sl_input.c -
     * the harness supplies stub devices and stub front-end answers, and nothing
     * was added to production source for it. What is NOT tested here is the
     * window-to-logical mapping and the hit testing: the mapping lives in
     * src/native/sl_menu_pointer.c against the game's own view rectangle and
     * the renderer's own presented rectangle, neither of which links without
     * the game and GL, and the hit testing is Rare's and is not ours to test.
     * That half was MEASURED on the real binary instead, on the file select
     * screen, through a temporary trace that was removed afterwards:
     *
     *   960x720 window, presented rect (0,0,960,720), the game's own view
     *   rectangle (0,0,440,330) -> pointer (608,393) gave uv (0.6333,0.5458)
     *   and cursor (278.7,180.1). 608/960*440 = 278.67, 393/720*330 = 180.13.
     *
     *   The presented rectangle was then read at 960x720, 640x480 and 800x600
     *   and returned exactly the window each time, while the view rectangle
     *   stayed 440x330 - so the mapping carries no window-size constant.
     * ================================================================== */
    printf("\n== the absolute pointer: availability ==\n");
    {
        int px, py, ww, wh, ok;
        unsigned motion_a, motion_b;

        reset();
        sl_input_live_focus(1);
        stub_mousefocus = 1;
        stub_px = 100; stub_py = 100;

        /* Gameplay: no pointer, whatever the mouse is doing. This is the line
         * that keeps the accepted mouse-look path from being disturbed. */
        stub_menu = 0; stub_ptrmenu = 0; stub_clickadv = 0;
        poll();
        ck(sl_input_pointer_get(&px, &py, &ww, &wh, &motion_a) == 0,
           "no pointer during play", st());

        /* A front-end menu that is NOT a cursor menu (the boot chain). */
        stub_menu = 2; stub_ptrmenu = 0; stub_clickadv = 1;
        poll();
        ck(sl_input_pointer_get(&px, &py, &ww, &wh, &motion_a) == 0,
           "no pointer on the boot chain (no cursor to move)", st());

        /* A cursor menu: the pointer is published, in window pixels, with the
         * window size the renderer set its viewport from. */
        stub_ptrmenu = 1;
        poll();
        ok = sl_input_pointer_get(&px, &py, &ww, &wh, &motion_a);
        ck(ok && px == 100 && py == 100 && ww == 960 && wh == 720,
           "a cursor menu publishes position and window size", st());

        printf("\n== ownership: the serial moves only when the mouse does ==\n");
        poll();
        sl_input_pointer_get(&px, &py, &ww, &wh, &motion_b);
        ck(motion_b == motion_a,
           "a stationary pointer does NOT advance the serial", st());

        stub_px = 101;
        poll();
        sl_input_pointer_get(&px, &py, &ww, &wh, &motion_b);
        ck(motion_b != motion_a && px == 101,
           "one pixel of real motion advances the serial", st());

        /* Focus loss drops the pointer, and coming back is a fresh baseline
         * rather than a phantom jump - the serial must not advance across it. */
        motion_a = motion_b;
        sl_input_live_focus(0); poll();
        ck(sl_input_pointer_get(&px, &py, &ww, &wh, &motion_b) == 0,
           "focus loss drops the pointer", st());
        sl_input_live_focus(1);
        stub_px = 500; stub_py = 400;      /* moved while we were not looking */
        poll();
        sl_input_pointer_get(&px, &py, &ww, &wh, &motion_b);
        ck(motion_b == motion_a,
           "the first sample after focus regain is a baseline, not a move", st());
        poll();
        sl_input_pointer_get(&px, &py, &ww, &wh, &motion_b);
        ck(motion_b == motion_a,
           "and still a baseline while it stays still", st());
    }

    printf("\n== the pointer state machine: one mode per screen class ==\n");
    {
        reset();
        sl_input_live_focus(1);
        stub_px = 100; stub_py = 100; stub_mousefocus = 1;

        /* GAMEPLAY. Unchanged, and asserted first so a regression here is
         * unambiguous: relative mode on, cursor hidden, grabbed, confined. */
        stub_menu = 0; stub_ptrmenu = 0; stub_clickadv = 0;
        sl_input_live_click(SDL_BUTTON_LEFT, 1); poll();
        stub_buttons = 0; sl_input_live_release(SDL_BUTTON_LEFT); poll();
        ck(stub_relmode == 1 && stub_cursor == 0 && stub_wingrab == 1
           && stub_rect_set,
           "gameplay: relative ON, cursor hidden, confined", st());

        /* THE FRONT END. Confined and grabbed like gameplay, and the host
         * cursor hidden like gameplay - but relative mode OFF, which is the one
         * difference and the whole point: the absolute position has to stay
         * meaningful. The cursor the player sees there is the GAME's, drawn by
         * frontDrawCursor (front.c:1251) from the cursor_h_pos/cursor_v_pos
         * this feature moves; leaving the host's up as well put two pointers on
         * screen (owner-reported on file select, 2026-09-07). */
        stub_menu = 2; stub_ptrmenu = 1; stub_clickadv = 1; poll();
        ck(stub_relmode == 0, "front end: relative mode OFF", st());
        ck(stub_cursor == 0, "front end: HOST cursor hidden (the game draws one)", st());
        ck(stub_wingrab == 1 && stub_rect_set,
           "front end: still confined to the window", st());
        {
            int px, py, ww, wh; unsigned m;
            ck(sl_input_pointer_get(&px, &py, &ww, &wh, &m) == 1,
               "front end: the absolute position survives confinement", st());
        }

        /* THE BOOT CHAIN is the same screen class - confined, visible - and it
         * gets there without any pointer menu being active. */
        /* THE BOOT CHAIN is a different screen class: it draws no pointer of
         * its own, so the HOST cursor stays - owner's call, 2026-09-07,
         * "Regular mouse cursor is fine for the legal, and other logos". It is
         * still confined, and still has no relative mode. */
        stub_ptrmenu = 0; stub_clickadv = 1; poll();
        ck(stub_relmode == 0 && stub_wingrab == 1,
           "boot chain: confined, no relative mode", st());
        ck(stub_cursor == 1,
           "boot chain: host cursor VISIBLE (nothing else draws one)", st());

        /* And the two menu classes differ by that one call and nothing else. */
        stub_ptrmenu = 1; poll();
        ck(stub_cursor == 0 && stub_relmode == 0 && stub_wingrab == 1,
           "boot chain -> file select hides the host cursor only", st());

        /* THE WATCH before any click is FREE like every uncaptured screen:
         * released, host cursor shown (#40 changed only the ARMED case, which
         * the capture section above covers). No click has armed it here. */
        sl_input_live_focus(0); sl_input_live_focus(1);   /* disarm: a focus loss */
        stub_menu = 1; stub_ptrmenu = 0; stub_clickadv = 0; poll();
        ck(stub_relmode == 0 && stub_cursor == 1 && !stub_wingrab
           && !stub_rect_set,
           "the watch, unarmed, releases the pointer AND shows the cursor", st());

        /* FOCUS LOSS never traps the player, in either confined mode. */
        stub_menu = 2; stub_ptrmenu = 1; stub_clickadv = 1; poll();
        ck(stub_wingrab == 1, "front end confined again", st());
        sl_input_live_focus(0); poll();
        ck(!stub_wingrab && !stub_rect_set && stub_cursor == 1,
           "focus loss releases the front-end confinement too", st());
        sl_input_live_focus(1); poll();
        ck(stub_wingrab == 1 && stub_relmode == 0 && stub_cursor == 0,
           "focus regain restores confinement with no click", st());

        /* THE HANDOFF, both directions, with nothing layered or double-applied:
         * the ONLY difference between the two states is relative mode and the
         * cursor. */
        stub_menu = 0; stub_ptrmenu = 0; stub_clickadv = 0;
        sl_input_live_click(SDL_BUTTON_LEFT, 1);
        stub_buttons = 0; sl_input_live_release(SDL_BUTTON_LEFT); poll();
        ck(stub_relmode == 1 && stub_cursor == 0 && stub_wingrab == 1,
           "front end -> gameplay hands off to relative look", st());
        stub_menu = 2; stub_ptrmenu = 1; stub_clickadv = 1; poll();
        ck(stub_relmode == 0 && stub_wingrab == 1,
           "gameplay -> front end drops relative mode, stays confined", st());
        ck(stub_cursor == 0,
           "and the host cursor stays hidden across that handoff", st());
    }

    printf("\n== who is pointing: the mouse, or the keyboard/pad ==\n");
    {
        int px, py, ww, wh; unsigned m;

        /* This is the platform half of the screen-entry resync. GoldenEye
         * re-places its cursor on every menu transition (front.c:3080, :3453,
         * :3694, :1376); the frontend layer re-asserts the pointer position
         * over that reset ONLY when the mouse is the thing pointing, so that a
         * pad player still gets Rare's placement. This flag is that condition,
         * and getting it wrong in either direction is a visible defect - a
         * cursor that jumps to centre, or one that fights a gamepad. */
        /* A GENUINELY FRESH session, because "has never moved" is a property
         * of one, and earlier sections of this harness have moved the mouse
         * plenty. shutdown clears the claim along with the rest of the pointer
         * state; the re-init below is the same pattern the confinement section
         * above uses. Without this the first poll counts the jump from
         * wherever the last section left the pointer as real motion, which is
         * correct behaviour being measured against the wrong initial state. */
        sl_input_live_shutdown();
        sl_input_live_set_window((void *) &checks);
        stub_win = (SDL_Window *) &checks;
        reset();
        sl_input_live_focus(1);
        stub_menu = 2; stub_ptrmenu = 1; stub_clickadv = 1;
        stub_mousefocus = 1; stub_px = 300; stub_py = 300;
        poll();

        /* A mouse that has never moved does NOT own the cursor: a pad session
         * must behave exactly as it did before any of this existed. This is the
         * assertion that keeps the screen-entry resync from firing for a pad
         * player and stealing Rare's cursor placement from them. */
        ck(sl_input_pointer_owns() == 0,
           "an unmoved mouse does not own the cursor", st());

        stub_px = 301; poll();
        ck(sl_input_pointer_owns() == 1,
           "one pixel of real motion takes the cursor", st());

        /* Keyboard menu navigation takes it straight back. The front end moves
         * its cursor from the STICK alone (front.c:1148), so a deflection is
         * the whole test. */
        press(SDL_SCANCODE_DOWN, 2, 0);
        ck(sl_input_pointer_owns() == 0,
           "arrow-key menu navigation takes the cursor back", st());
        reset(); stub_menu = 2; poll();
        ck(sl_input_pointer_owns() == 0,
           "and a stationary mouse does not steal it again", st());

        press(SDL_SCANCODE_W, 2, 0);
        ck(sl_input_pointer_owns() == 0, "W in a menu likewise", st());

        /* Moving again re-takes it, which is the only way back. */
        reset(); stub_menu = 2; stub_px = 320; poll();
        ck(sl_input_pointer_owns() == 1,
           "moving the mouse again re-takes the cursor", st());

        /* A CONFIRM must not take the cursor away from a mouse still in use -
         * pressing A does not move a cursor, so buttons are not tested. */
        sl_input_live_click(SDL_BUTTON_LEFT, 1); poll();
        ck(sl_input_pointer_owns() == 1,
           "clicking to confirm does not surrender the cursor", st());
        stub_buttons = 0; sl_input_live_release(SDL_BUTTON_LEFT); poll();

        /* Focus loss drops it, so coming back cannot resync from a pointer
         * that wandered across the desktop meanwhile. */
        sl_input_live_focus(0); poll();
        ck(sl_input_pointer_owns() == 0, "focus loss drops the cursor claim", st());
        sl_input_live_focus(1); poll();
        ck(sl_input_pointer_owns() == 0,
           "and focus regain alone does not restore it", st());
        stub_px = 340; poll();
        ck(sl_input_pointer_owns() == 1, "real motion after regain does", st());
        (void) sl_input_pointer_get(&px, &py, &ww, &wh, &m);
    }

    printf("\n== the click edge: one press, one confirm ==\n");
    {
        int i, seen;

        reset();
        sl_input_live_focus(1);
        stub_menu = 2; stub_ptrmenu = 1; stub_clickadv = 1;
        poll();

        /* One press -> N64 A, held just long enough to survive latching, then
         * gone. The GAME turns that into exactly one edge (joy.c:395); what
         * this asserts is that the layer stops on its own and never re-raises
         * it without a new press. */
        sl_input_live_click(SDL_BUTTON_LEFT, 1);
        seen = 0;
        for (i = 0; i < 12; i++) { poll(); if (out_b & SL_BTN_A) seen++; }
        ck(seen > 0 && seen < 12, "one click asserts A for a bounded burst", st());
        ck((out_b & SL_BTN_A) == 0, "and it has stopped by the end", st());

        /* HELD, not re-pressed: SDL raises one SDL_MOUSEBUTTONDOWN per press,
         * so there is nothing to repeat. Modelled by holding the button in the
         * device state and NOT calling click again. */
        stub_buttons = SDL_BUTTON(SDL_BUTTON_LEFT);
        seen = 0;
        for (i = 0; i < 12; i++) { poll(); if (out_b & SL_BTN_A) seen++; }
        ck(seen == 0, "a HELD button never confirms again", st());
        stub_buttons = 0;
        sl_input_live_release(SDL_BUTTON_LEFT);

        /* The second edge of a double click is refused, so a double click
         * cannot activate something on the screen the first click opened. */
        sl_input_live_click(SDL_BUTTON_LEFT, 2);
        seen = 0;
        for (i = 0; i < 6; i++) { poll(); if (out_b & SL_BTN_A) seen++; }
        ck(seen == 0, "the second edge of a double click does NOT confirm", st());

        /* Right button is aim, not confirm. */
        sl_input_live_click(SDL_BUTTON_RIGHT, 1);
        seen = 0;
        for (i = 0; i < 6; i++) { poll(); if (out_b & SL_BTN_A) seen++; }
        ck(seen == 0, "the right button does not confirm", st());

        /* And nothing confirms where a button press would do nothing. */
        stub_ptrmenu = 0; stub_clickadv = 0;
        sl_input_live_click(SDL_BUTTON_LEFT, 1);
        seen = 0;
        for (i = 0; i < 6; i++) { poll(); if (out_b & SL_BTN_A) seen++; }
        ck(seen == 0, "no confirm where a press would do nothing", st());
    }

    printf("\n== the click beside the image: a wider aspect's band confirms nothing ==\n");
    {
        int i, seen;

        /* The widescreen cursor repair. On a cursor menu the game's cursor is
         * left where the pointer crossed out of the 4:3 image (never clamped
         * to the edge), so a click in the band beside it must not confirm
         * the item the cursor still sits on. Measured before the repair on a
         * 1280x720 MODE SELECT: probe (40,360) + click -> `menu 6 -> 26`. */
        reset();
        sl_input_live_release(SDL_BUTTON_LEFT);
        sl_input_live_focus(1);
        stub_menu = 2; stub_ptrmenu = 1; stub_clickadv = 1; stub_ptrover = 0;
        poll();
        sl_input_live_click(SDL_BUTTON_LEFT, 1);
        seen = 0;
        for (i = 0; i < 12; i++) { poll(); if (out_b & SL_BTN_A) seen++; }
        ck(seen == 0, "a click in the band beside the image confirms nothing", st());

        /* ... and the press it was is still a menu press: should the screen
         * change under it, the held button never arrives as fire. */
        stub_buttons = SDL_BUTTON(SDL_BUTTON_LEFT);
        stub_menu = 0;
        for (i = 0; i < 6; i++) poll();
        ck((out_b & SL_BTN_Z) == 0, "a dropped band click held into play does not fire", st());
        stub_buttons = 0;
        sl_input_live_release(SDL_BUTTON_LEFT);

        /* Back over the image the same click confirms: the gate is the
         * position, nothing else. */
        reset();
        sl_input_live_focus(1);
        stub_menu = 2; stub_ptrmenu = 1; stub_clickadv = 1; stub_ptrover = 1;
        poll();
        sl_input_live_click(SDL_BUTTON_LEFT, 1);
        seen = 0;
        for (i = 0; i < 12; i++) { poll(); if (out_b & SL_BTN_A) seen++; }
        ck(seen > 0, "the same click over the image confirms", st());
        sl_input_live_release(SDL_BUTTON_LEFT);

        /* The boot chain has no cursor and nothing to mis-hit: its any-button
         * skip is not gated by where the pointer rests. */
        reset();
        sl_input_live_focus(1);
        stub_menu = 2; stub_ptrmenu = 0; stub_clickadv = 1; stub_ptrover = 0;
        poll();
        sl_input_live_click(SDL_BUTTON_LEFT, 1);
        seen = 0;
        for (i = 0; i < 12; i++) { poll(); if (out_b & SL_BTN_A) seen++; }
        ck(seen > 0, "the boot chain's any-button skip is not gated by the pointer's position", st());
        sl_input_live_release(SDL_BUTTON_LEFT);
        stub_ptrover = 1;
    }

    printf("\n== THE CLICK THAT STARTS THE LEVEL MUST NOT FIRE IT ==\n");
    {
        int i;

        /* The exact sequence the owner runs: click AGENT on a front-end
         * screen, then the level comes up. The click armed the capture like
         * any other click, so by the first gameplay frame the grab is on and
         * read_mouse's button gate is open - and the press is still down. */
        reset();
        sl_input_live_focus(1);
        stub_menu = 2; stub_ptrmenu = 1; stub_clickadv = 1;
        poll();
        sl_input_live_click(SDL_BUTTON_LEFT, 1);
        stub_buttons = SDL_BUTTON(SDL_BUTTON_LEFT);   /* still held */
        poll();

        stub_menu = 0;                                /* the level starts */
        for (i = 0; i < 6; i++) poll();
        ck(stub_relmode == 1, "the grab engages for play with no extra click", st());
        ck((out_b & SL_BTN_Z) == 0,
           "the menu click does NOT arrive as the first shot", st());

        /* Releasing re-arms. The NEXT press is a fresh one and does fire. */
        stub_buttons = 0;
        sl_input_live_release(SDL_BUTTON_LEFT);
        poll();
        stub_buttons = SDL_BUTTON(SDL_BUTTON_LEFT);
        poll();
        ck((out_b & SL_BTN_Z) != 0, "the next press fires normally", st());

        /* A focus loss while the mark is set must not strand it: the button
         * may well be released over another window, where no up edge reaches
         * us, and a mark left standing would kill mouse fire for the run. */
        reset();
        stub_menu = 2; stub_ptrmenu = 1; stub_clickadv = 1;
        sl_input_live_focus(1); poll();
        sl_input_live_click(SDL_BUTTON_LEFT, 1);
        sl_input_live_focus(0); poll();               /* released elsewhere */
        sl_input_live_focus(1);
        sl_input_live_click(SDL_BUTTON_LEFT, 1);
        stub_menu = 0;
        for (i = 0; i < 6; i++) poll();
        stub_buttons = 0;
        sl_input_live_release(SDL_BUTTON_LEFT);
        poll();
        stub_buttons = SDL_BUTTON(SDL_BUTTON_LEFT);
        poll();
        ck((out_b & SL_BTN_Z) != 0,
           "a focus loss does not strand the mark and kill fire", st());
        stub_buttons = 0;
        sl_input_live_release(SDL_BUTTON_LEFT);
    }

    printf("\n%d checks, %d failed\n", checks, fails);
    return fails != 0;
}
