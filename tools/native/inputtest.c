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
#include <string.h>
#include "../../src/platform/sl_input.h"

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


/* No gamepad in this harness: the pad path has its own owner-validated
 * behaviour and this test is about the keyboard and mouse. read_pad exits at
 * the first of these. */
int  SDL_InitSubSystem(Uint32 flags) { (void) flags; return -1; }
int  SDL_NumJoysticks(void) { return 0; }
SDL_bool SDL_IsGameController(int i) { (void) i; return SDL_FALSE; }
SDL_GameController *SDL_GameControllerOpen(int i) { (void) i; return NULL; }
const char *SDL_GameControllerName(SDL_GameController *g) { (void) g; return "none"; }
SDL_bool SDL_GameControllerGetAttached(SDL_GameController *g) { (void) g; return SDL_FALSE; }
Sint16 SDL_GameControllerGetAxis(SDL_GameController *g, SDL_GameControllerAxis a) { (void) g; (void) a; return 0; }
Uint8 SDL_GameControllerGetButton(SDL_GameController *g, SDL_GameControllerButton b) { (void) g; (void) b; return 0; }
void SDL_GameControllerClose(SDL_GameController *g) { (void) g; }
/* Added by the multi-pad rescan (sl_input.c pad_rescan / read_pad). The list
 * is empty here - SDL_NumJoysticks returns 0 - so these are never reached with
 * a real device; they exist so the link succeeds. Without them this harness
 * had rotted out of buildability against the shipping file it is supposed to
 * be testing, which is the one failure mode a link-the-real-thing test has. */
SDL_JoystickID SDL_JoystickGetDeviceInstanceID(int i) { (void) i; return -1; }
SDL_Joystick *SDL_GameControllerGetJoystick(SDL_GameController *g) { (void) g; return NULL; }
SDL_JoystickID SDL_JoystickInstanceID(SDL_Joystick *j) { (void) j; return -1; }
Uint32 SDL_WasInit(Uint32 flags) { (void) flags; return 0; }

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
void sl_run_mark(unsigned n) { (void) n; }
unsigned sl_record_index(void) { return 0; }

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

/* --------------------------------------------------------------------------
 * harness
 * ----------------------------------------------------------------------- */
#define SL_BTN_A     0x8000
#define SL_BTN_B     0x4000
#define SL_BTN_Z     0x2000
#define SL_BTN_START 0x1000

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

int main(void)
{
    int i, steps_up, steps_down, saw_centre;

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
    stub_menu = 1; poll();
    ck(stub_relmode == 0 && stub_cursor == 1, "a menu releases the pointer", st());
    stub_menu = 0; poll();
    ck(stub_relmode == 1 && stub_cursor == 0,
       "leaving the menu re-captures with no click", st());

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
    ck(!stub_rect_set, "a menu removes the barrier", st());
    stub_menu = 0; poll();
    ck(stub_rect_set, "leaving the menu re-arms it", st());
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
    press(SDL_SCANCODE_E, 0, 0);
    ck(out_b == SL_BTN_B, "E -> N64 B (use / reload)", st());
    press(SDL_SCANCODE_R, 0, 0);
    ck(out_b == SL_BTN_A, "R -> N64 A (weapon cycle)", st());
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

        /* THE WATCH is deliberately unchanged: the pointer is released. */
        stub_menu = 1; stub_ptrmenu = 0; stub_clickadv = 0; poll();
        ck(stub_relmode == 0 && stub_cursor == 1 && !stub_wingrab
           && !stub_rect_set,
           "the watch releases the pointer AND shows the cursor", st());

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
