/* SDL2 + OpenGL backend.
 *
 * Deliberately thin: window, GL context, clear, present, and a quit check.
 * Everything the RCP does still has to be built on top - this is the surface
 * a display-list interpreter will draw into, not a renderer itself.
 *
 * Built -m32 like the rest of the native skeleton (the decomp assumes 32-bit
 * pointers), which works because libsdl2-dev:i386 and the i386 GL runtime are
 * installed. If a machine lacks those, sl_gfx_select() falls back to null and
 * the sim still runs.
 */
#ifndef __sgi
#include "sl_gfx.h"
#include "../platform/sl_input.h"
#include "../platform/sl_display.h"     /* #45: the aspect selection drives the window's width */
#include "../platform/sl_window.h"      /* #52: WINDOW MODE / RESOLUTION / VSYNC - the request seam */
#include <SDL2/SDL.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>

static SDL_Window *win;
static SDL_GLContext ctx;

/* ---- B-124, retired to a diagnostic arm by B-125.
 *
 * Surface's dome is authored as TWO opposite-wound skins of the same quads -
 * a shaded interior pass, then a white exterior pass - same matrices (clip
 * rows bitwise identical at every shared vertex), z-compare and z-write on
 * both, no decal. Honouring their cull bits mis-culls one mirrored half
 * (measured: SL_CULL=1 and =2 each solve one half and break the other, the
 * same mixed-winding wall B-114 hit at Dam), so on hardware the passes are
 * resolved by DEPTH: the later (white) pass passes the RDP's tolerant
 * compare and wins - a mostly white dome outside, the marble inside, dark
 * only where the white skin is absent (the observatory slit).
 *
 * B-124 read that as "the RDP's coarse z quantizes the two skins to the same
 * value" and built this 16-bit offscreen depth attachment to make the tie
 * real. MEASURED WRONG at owner mark 20260912-230216-001: the two skins'
 * depths differ by +1.8, +3.0 and +1.8 sixteen-bit buckets at three of the
 * four quad centres around the crosshair (the quads are not planar and the
 * passes triangulate them on opposite diagonals), so no storage precision
 * makes them ties. 16 bits turned the 24-bit gores into a lattice of dark
 * dashes along the bucket contours - the residual hash the owner rejected -
 * and an offline rasterization of the mark's own vertex data reproduces
 * that lattice at 84% pixel agreement (24-bit: 45%). What resolves the
 * skins on hardware is the RDP compare's slope-proportional TOLERANCE, not
 * its storage width; that is now emulated for authored redraws in
 * sl_gfx_dl.c (B-125), and the scene renders to the window's native 24-bit
 * buffer again - with a stencil plane, which the redraw path needs.
 *
 * SL_Z_QUANT=1 re-creates the 16-bit FBO as an A/B arm: it reproduces the
 * B-124 lattice on demand (no stencil plane there, so the redraw path is
 * inert under it by construction). Any creation failure falls back to the
 * window, reported, never fatal. */
typedef void   (APIENTRY *sl_pfn_GenFramebuffers)(GLsizei, GLuint *);
typedef void   (APIENTRY *sl_pfn_BindFramebuffer)(GLenum, GLuint);
typedef void   (APIENTRY *sl_pfn_GenRenderbuffers)(GLsizei, GLuint *);
typedef void   (APIENTRY *sl_pfn_BindRenderbuffer)(GLenum, GLuint);
typedef void   (APIENTRY *sl_pfn_RenderbufferStorage)(GLenum, GLenum, GLsizei, GLsizei);
typedef void   (APIENTRY *sl_pfn_FramebufferRenderbuffer)(GLenum, GLenum, GLenum, GLuint);
typedef GLenum (APIENTRY *sl_pfn_CheckFramebufferStatus)(GLenum);
typedef void   (APIENTRY *sl_pfn_BlitFramebuffer)(GLint, GLint, GLint, GLint,
                                                  GLint, GLint, GLint, GLint,
                                                  GLbitfield, GLenum);
#define SL_GL_FRAMEBUFFER          0x8D40u
#define SL_GL_READ_FRAMEBUFFER     0x8CA8u
#define SL_GL_DRAW_FRAMEBUFFER     0x8CA9u
#define SL_GL_RENDERBUFFER         0x8D41u
#define SL_GL_COLOR_ATTACHMENT0    0x8CE0u
#define SL_GL_DEPTH_ATTACHMENT     0x8D00u
#define SL_GL_FRAMEBUFFER_COMPLETE 0x8CD5u
#define SL_GL_RGBA8                0x8058u
#define SL_GL_DEPTH_COMPONENT16    0x81A5u

static GLuint sl_fbo, sl_fbo_color, sl_fbo_depth;
static int    sl_fbo_w, sl_fbo_h;
static int    s_window_pinned;            /* SL_WINDOW_POS given: no re-centring (#45) */
static sl_pfn_BindFramebuffer sl_BindFramebuffer;
static sl_pfn_BlitFramebuffer sl_BlitFramebuffer;

/* ---- #52: the PC display modes' backend state ---------------------------
 *
 * s_launch_w / _h   the launcher's initial window (SL_WINDOW_SIZE, else the
 *                   compiled 640x480) - what a windowed client falls back to
 *                   when no height was chosen or the chosen one is not
 *                   offered: the accepted launch, bit for bit.
 * s_windowed_w / _h the windowed client size to RETURN to from a fullscreen
 *                   mode (the last one the window had while windowed; the
 *                   launcher's at first), so leaving BORDERLESS / FULLSCREEN
 *                   restores the window the player left, never the desktop.
 * s_app_*           what the last apply established, so the per-frame check
 *                   costs one comparison and a frame never mixes two states.
 * s_display         the SDL display the window is on, whose mode list and
 *                   desktop size sl_window.c holds; re-read at every apply. */
static int s_launch_w, s_launch_h;
static int s_windowed_w, s_windowed_h;
static int s_app_mode = -1, s_app_aspect = -1;
static int s_display = -1;

static void sdl_apply_display(int at_init);

static void sl_fbo16_create(int w, int h)
{
    sl_pfn_GenFramebuffers         GenFramebuffers;
    sl_pfn_GenRenderbuffers        GenRenderbuffers;
    sl_pfn_BindRenderbuffer        BindRenderbuffer;
    sl_pfn_RenderbufferStorage     RenderbufferStorage;
    sl_pfn_FramebufferRenderbuffer FramebufferRenderbuffer;
    sl_pfn_CheckFramebufferStatus  CheckFramebufferStatus;
    const char *zq = getenv("SL_Z_QUANT");

    /* B-125: OFF unless asked for. The default frame renders to the window
     * (24-bit depth + 8-bit stencil, requested at context creation). */
    if (!(zq != NULL && *zq == '1' && zq[1] == '\0')) return;
    fprintf(stderr, "sightline gfx: SL_Z_QUANT=1 - B-124 16-bit offscreen"
                    " depth arm requested (diagnostic; no stencil plane, so"
                    " the B-125 redraw path is inert under it)\n");
    GenFramebuffers         = (sl_pfn_GenFramebuffers)         SDL_GL_GetProcAddress("glGenFramebuffers");
    sl_BindFramebuffer      = (sl_pfn_BindFramebuffer)         SDL_GL_GetProcAddress("glBindFramebuffer");
    GenRenderbuffers        = (sl_pfn_GenRenderbuffers)        SDL_GL_GetProcAddress("glGenRenderbuffers");
    BindRenderbuffer        = (sl_pfn_BindRenderbuffer)        SDL_GL_GetProcAddress("glBindRenderbuffer");
    RenderbufferStorage     = (sl_pfn_RenderbufferStorage)     SDL_GL_GetProcAddress("glRenderbufferStorage");
    FramebufferRenderbuffer = (sl_pfn_FramebufferRenderbuffer) SDL_GL_GetProcAddress("glFramebufferRenderbuffer");
    CheckFramebufferStatus  = (sl_pfn_CheckFramebufferStatus)  SDL_GL_GetProcAddress("glCheckFramebufferStatus");
    sl_BlitFramebuffer      = (sl_pfn_BlitFramebuffer)         SDL_GL_GetProcAddress("glBlitFramebuffer");
    if (!GenFramebuffers || !sl_BindFramebuffer || !GenRenderbuffers
        || !BindRenderbuffer || !RenderbufferStorage
        || !FramebufferRenderbuffer || !CheckFramebufferStatus
        || !sl_BlitFramebuffer) {
        fprintf(stderr, "sightline gfx: FBO entry points unresolved -"
                        " B-124 16-bit depth OFF (window buffer in use)\n");
        sl_BindFramebuffer = NULL;
        return;
    }
    GenFramebuffers(1, &sl_fbo);
    GenRenderbuffers(1, &sl_fbo_color);
    GenRenderbuffers(1, &sl_fbo_depth);
    BindRenderbuffer(SL_GL_RENDERBUFFER, sl_fbo_color);
    RenderbufferStorage(SL_GL_RENDERBUFFER, SL_GL_RGBA8, w, h);
    BindRenderbuffer(SL_GL_RENDERBUFFER, sl_fbo_depth);
    RenderbufferStorage(SL_GL_RENDERBUFFER, SL_GL_DEPTH_COMPONENT16, w, h);
    sl_BindFramebuffer(SL_GL_FRAMEBUFFER, sl_fbo);
    FramebufferRenderbuffer(SL_GL_FRAMEBUFFER, SL_GL_COLOR_ATTACHMENT0,
                            SL_GL_RENDERBUFFER, sl_fbo_color);
    FramebufferRenderbuffer(SL_GL_FRAMEBUFFER, SL_GL_DEPTH_ATTACHMENT,
                            SL_GL_RENDERBUFFER, sl_fbo_depth);
    if (CheckFramebufferStatus(SL_GL_FRAMEBUFFER) != SL_GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "sightline gfx: FBO incomplete - B-124 16-bit depth"
                        " OFF (window buffer in use)\n");
        sl_BindFramebuffer(SL_GL_FRAMEBUFFER, 0);
        sl_BindFramebuffer = NULL;
        sl_fbo = 0;
        return;
    }
    sl_fbo_w = w; sl_fbo_h = h;
    fprintf(stderr, "sightline gfx: B-124 offscreen 16-bit depth ACTIVE"
                    " (%dx%d RGBA8 + DEPTH_COMPONENT16; diagnostic arm)\n",
            w, h);
}

static int sdl_init(int w, int h, const char *title)
{
#ifdef SDL_MAIN_HANDLED
    /* We keep our own plain int main(void) rather than SDL2's WinMain shim,
     * so SDL has to be told the main-thread setup it normally does for itself
     * has already happened. Harmless everywhere else; on Windows SDL_Init
     * refuses to proceed without it. See docs/decisions/windows-sdl-main.md */
    SDL_SetMainReady();
#endif
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "sightline gfx: SDL_Init failed: %s\n", SDL_GetError());
        return 0;
    }
    /* A no-driver/no-display init reports SUCCESS if SDL has been stubbed out
     * (see tools/native/build.sh) - check rather than trust the return code. */
    if (SDL_GetCurrentVideoDriver() == NULL || SDL_GetNumVideoDisplays() < 1) {
        fprintf(stderr, "sightline gfx: SDL initialised with no video driver - "
                        "is SDL stubbed? falling back to headless\n");
        return 0;
    }
    /* Which BACKEND came up, said once and unconditionally. SDL picks this
     * from SDL_VIDEODRIVER and the environment, and nothing else in the build
     * prints it - the only other SDL_GetCurrentVideoDriver() calls are inside
     * the mouse-confinement messages, which never fire unless the player has
     * already clicked to capture. A round was already lost to not being able
     * to tell from outside which configuration a launch was running (the
     * SL_MOUSE_DX_SIGN episode); this is the same class of ambiguity and the
     * same one-line answer. Permanent, like the mouse-convention banner. */
    fprintf(stderr, "sightline gfx: SDL video driver = %s\n",
            SDL_GetCurrentVideoDriver());

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    /* B-124 (correcting B-123). SDL_GL_DEPTH_SIZE is a MINIMUM: B-123's
     * request for 16 bits was measured INERT - the driver handed back a
     * 24-bit window buffer regardless (the stored centre depth at owner mark
     * 20260912-222555-001 reads back as an exact multiple of 1/2^24-1, not
     * of 1/2^16-1). B-125 renders the scene here again, at that native
     * depth, and needs a stencil plane beside it: the authored-redraw path
     * in sl_gfx_dl.c marks the pixels its tolerant colour pass won and
     * writes their exact depth through that mark. Both are minimums; the
     * bits actually granted are read back per frame (GL_STENCIL_BITS) and
     * the redraw path stays off when none came. */
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    s_launch_w = w; s_launch_h = h;
    s_windowed_w = w; s_windowed_h = h;
    {
        /* SL_WINDOW_POS=x,y (developer): the initial window position, and
         * the window is neither activated when shown nor re-centred by the
         * aspect resize - a capture run can park itself off-screen and take
         * nothing from a desktop in use (the qol10 evidence runs). Absent
         * (every player launch): centred, as before. */
        int px = SDL_WINDOWPOS_CENTERED, py = SDL_WINDOWPOS_CENTERED;
        Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN;
        const char *wp = getenv("SL_WINDOW_POS");
        int mode = sl_window_mode();
        int sw = 0, sh = 0;
        if (wp != NULL && sscanf(wp, "%d,%d", &px, &py) == 2) {
            s_window_pinned = 1;
            SDL_SetHint(SDL_HINT_WINDOW_NO_ACTIVATION_WHEN_SHOWN, "1");
        } else {
            px = SDL_WINDOWPOS_CENTERED; py = SDL_WINDOWPOS_CENTERED;
        }
        /* #52: THE WINDOW IS CREATED IN ITS PERSISTED STATE, not flashed
         * through the launcher's. A chosen windowed height (config.ini
         * window_height, one the display offers) is the creation size,
         * at the aspect's width - SL_WINDOW_SIZE then names nothing but
         * the fallback, said out loud so a stale launcher size never
         * silently fights the setting. A fullscreen mode creates the
         * window HIDDEN, sdl_apply_display below takes it to the mode
         * through the one transition path every later change uses, and
         * only then is it shown. With nothing chosen (the default, every
         * config before #52) the flags and the size are exactly the
         * pre-#52 ones: the oracle. The display consulted before a window
         * exists is display 0, where SDL_WINDOWPOS_CENTERED lands; the
         * apply re-reads the display the window actually landed on. */
        if (sl_settings_active() && mode == SL_WINDOW_WINDOWED && sl_window_stored_windowed(&sw, &sh)) {
            struct sl_window_list modes, wl;
            int nm = SDL_GetNumDisplayModes(0), i, n = 0;
            int mw[256], mh[256];
            SDL_DisplayMode dm;
            int tw, th, used;
            for (i = 0; i < nm && n < 256; i++) {
                SDL_DisplayMode m;
                if (SDL_GetDisplayMode(0, i, &m) == 0) { mw[n] = m.w; mh[n] = m.h; n++; }
            }
            if (SDL_GetDesktopDisplayMode(0, &dm) != 0) { dm.w = 0; dm.h = 0; }
            sl_window_list_modes(mw, mh, n, &modes);
            sl_window_list_windowed(&modes, dm.w, dm.h, sl_aspect_active() ? sl_aspect_ratio() : SL_ASPECT_4_3, &wl);
            used = sl_window_pick_windowed(&wl, sh, h, sl_aspect_active() ? sl_aspect_ratio() : SL_ASPECT_4_3, &tw, &th);
            if (used) {
                const char *ws = getenv("SL_WINDOW_SIZE");
                fprintf(stderr, "sightline gfx: config.ini window_height=%d chosen -> the window is created %dx%d"
                                " (the aspect's width); %s%s stands aside as the fallback only\n",
                        sh, tw, th, ws != NULL ? "SL_WINDOW_SIZE=" : "the compiled ", ws != NULL ? ws : "640x480");
                w = tw; h = th;
            } else {
                fprintf(stderr, "sightline gfx: config.ini window_height=%d is not a height display 0 offers"
                                " (desktop %dx%d) -> the launcher's %dx%d (not rewritten)\n",
                        sh, dm.w, dm.h, w, h);
            }
        } else if (sl_settings_active() && mode != SL_WINDOW_WINDOWED) {
            flags = SDL_WINDOW_OPENGL;      /* hidden until the mode is established */
        }
        win = SDL_CreateWindow(title, px, py, w, h, flags);
    }
    if (win == NULL) {
        fprintf(stderr, "sightline gfx: CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 0;
    }
    ctx = SDL_GL_CreateContext(win);
    if (ctx == NULL) {
        fprintf(stderr, "sightline gfx: GL context failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(win); win = NULL;
        SDL_Quit();
        return 0;
    }
    SDL_GL_SetSwapInterval(0);          /* replay sets the pace, not vsync (the #52 VSYNC
                                         * setting is applied below, read back, and
                                         * defaults to exactly this) */
    /* The input layer needs the window itself: holding the pointer takes
     * SDL_SetWindowMouseGrab as well as relative mode (see set_grab in
     * src/platform/sl_input.c), and SDL_GetKeyboardFocus returns NULL in
     * exactly the case where the grab has to be re-asserted. */
    sl_input_live_set_window(win);
    fprintf(stderr, "sightline gfx: SDL window %dx%d, GL \"%s\"\n",
            w, h, (const char *) glGetString(GL_VERSION));
    s_windowed_w = w; s_windowed_h = h;
    /* #52: the persisted mode and vsync, through the one transition path;
     * a window created hidden for a fullscreen mode is shown once it is
     * there (or once the fallback to windowed is). */
    sdl_apply_display(1);
    if (!(SDL_GetWindowFlags(win) & SDL_WINDOW_SHOWN))
        SDL_ShowWindow(win);
    {   /* B-125: what the window actually came with. Said once, because the
         * redraw path's stencil pass is gated on it and a silent zero would
         * be the B-123 class of failure (a request the driver ignored). */
        GLint zb = 0, sb = 0;
        glGetIntegerv(GL_DEPTH_BITS, &zb);
        glGetIntegerv(GL_STENCIL_BITS, &sb);
        fprintf(stderr, "sightline gfx: window depth %d bits, stencil %d bits\n",
                (int) zb, (int) sb);
    }
    sl_fbo16_create(w, h);            /* B-124 diagnostic arm, SL_Z_QUANT=1 only;
                                       * falls back to the window on any failure */
    return 1;
}

/* Resolve a GL entry point newer than the <GL/gl.h> this build compiles
 * against.
 *
 * B-045's lesson, in one function: glFogCoordf is GL 1.4, Mesa's header
 * declares only through 1.3, and calling it anyway was an ABI violation -
 * default argument promotion pushed a double where the entry point read a
 * float, so a correct value went in and an unrelated bit pattern came out,
 * with every telemetry reading healthy. -Werror=implicit-function-declaration
 * now catches the CALL; this is how the DECLARATION gets made honestly, with
 * a typedef the caller writes out in full.
 *
 * NULL before the context exists, and NULL when this backend is not the one
 * that came up - so a caller has to have a fallback, and the shader path does. */
void *sl_gl_proc(const char *name);
void *sl_gl_proc(const char *name)
{
    if (ctx == NULL) return NULL;
    return SDL_GL_GetProcAddress(name);
}

/* #45 / #52. THE WINDOW'S STATE: MODE, SIZE, VSYNC - and the aspect's width.
 *
 * Owner contract (#45, 2026-09-18): the selected aspect is the SHAPE of the
 * presentation at the CURRENT VERTICAL SIZE - widescreen means a WIDER
 * window at the same height, never a strip inside the old one. So in
 * windowed mode the window's client height is kept and its width becomes
 * height x aspect: 960x720 -> 1280x720 at 16:9 -> 2560x720 at 32:9, live when
 * the setting changes (SDL_SetWindowSize, the same context; the window is
 * re-centred on its display so a wide window does not run off the right
 * edge) and at the first frame from the persisted setting.
 *
 * Precedence: SL_WINDOW_SIZE (play.ps1 -Size) names the INITIAL window - its
 * height is kept, its width stands only while it agrees with the aspect; the
 * aspect wins on the width. At 4:3 with the accepted 960x720 nothing moves,
 * which is what keeps the 4:3 negative control byte-identical. Since #52 a
 * chosen RESOLUTION (config.ini window_height) names the height instead and
 * the launcher's is the fallback, logged when it is stood aside.
 *
 * #52 (2026-09-20): WINDOW MODE, RESOLUTION and VSYNC land here too - this
 * is THE ONE PLACE SDL's window is asked to change, once per frame at the
 * frame reset, and the one place its state is read back. The editors file a
 * request (sl_window.c); this takes it, remembers the working state, asks
 * SDL, reads back, and COMMITS to config.ini only what SDL confirmed -
 * otherwise it restores the working state (and, should that fail too, the
 * safest windowed state) and the file is untouched. Nothing else is told:
 * the renderer reads the window from GL at its next frame reset
 * (sl_gfx_dl.c g_window_vp), the #45 fit derives the content and safe rects
 * from that, the input layer re-reads the size every poll for its
 * confinement rect and the pointer layers read the safe rect back - the
 * pipeline that already existed carries a mode change to every consumer,
 * once, on the next frame. No level reload, no context recreation.
 *
 *   WINDOWED    SDL_SetWindowFullscreen(0), then the client size: the chosen
 *               height (or the one to return to) at the aspect's width.
 *   BORDERLESS  SDL_WINDOW_FULLSCREEN_DESKTOP: the desktop at its own mode,
 *               no mode switch; the fit rule paints the bars.
 *   FULLSCREEN  a real display mode: the chosen pair when the display
 *               offers it, else the desktop's, at the DESKTOP'S refresh rate
 *               (SDL_GetClosestDisplayMode with refresh 0 takes the
 *               display's highest - measured 240 Hz on the owner's panel -
 *               which is not what a 60 Hz sim wants driving its swap) -
 *               SDL_SetWindowDisplayMode, then
 *               SDL_SetWindowFullscreen(SDL_WINDOW_FULLSCREEN).
 *   VSYNC       SDL_GL_SetSwapInterval(0 / 1) on the live context, read back
 *               with SDL_GL_GetSwapInterval; a refusal restores the previous
 *               interval and writes nothing. The pacer (sl_ultra_shim.c
 *               sl_pace_frame) keeps setting the VI cadence either way: with
 *               the interval at 1 the swap merely waits for the vblank
 *               inside the frame the pacer already budgets.
 *
 * NOT resized by the aspect: a fullscreen, fullscreen-desktop, maximised or
 * minimised window (the framebuffer cannot be resized - the fit rule in
 * src/platform/sl_display.c applies: a framebuffer wider than the shape is
 * pillarboxed, one narrower keeps the FULL width and letterboxes top and
 * bottom), and the B-124 diagnostic offscreen arm (SL_Z_QUANT=1, whose
 * renderbuffers are sized once at init). A window the player has not been
 * given a way to resize by hand (no SDL_WINDOW_RESIZABLE, as before) stays
 * at the size the settings gave it. */

/* SDL's flags -> the mode. FULLSCREEN_DESKTOP carries the FULLSCREEN bit,
 * so it is tested first. */
static int sdl_read_mode(void)
{
    Uint32 f = SDL_GetWindowFlags(win);
    if ((f & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN_DESKTOP) return SL_WINDOW_BORDERLESS;
    if (f & SDL_WINDOW_FULLSCREEN) return SL_WINDOW_FULLSCREEN;
    return SL_WINDOW_WINDOWED;
}

/* The display the window is on: its mode list (deduped and bounded by
 * sl_window.c) and its desktop size, published for the editors' lists and
 * the fallbacks. Re-read at every apply, republished when the display
 * changed (a window dragged to another monitor - the pinned dev window
 * cannot be, but the player's can). Said once per display. */
static void sdl_publish_display(void)
{
    struct sl_window_list modes;
    int mw[256], mh[256];
    SDL_DisplayMode dm;
    int d = SDL_GetWindowDisplayIndex(win), nm, i, n = 0;
    if (d < 0) d = 0;
    if (d == s_display) return;
    s_display = d;
    nm = SDL_GetNumDisplayModes(d);
    for (i = 0; i < nm && n < 256; i++) {
        SDL_DisplayMode m;
        if (SDL_GetDisplayMode(d, i, &m) == 0) { mw[n] = m.w; mh[n] = m.h; n++; }
    }
    if (SDL_GetDesktopDisplayMode(d, &dm) != 0) { dm.w = 0; dm.h = 0; dm.refresh_rate = 0; }
    sl_window_list_modes(mw, mh, n, &modes);
    sl_window_publish_lists(&modes, dm.w, dm.h);
    fprintf(stderr, "sightline gfx: display %d \"%s\": desktop %dx%d@%d, %d modes -> %d sizes (%dx%d .. %dx%d)\n",
            d, SDL_GetDisplayName(d) ? SDL_GetDisplayName(d) : "?", dm.w, dm.h, dm.refresh_rate, nm, modes.n,
            modes.n > 0 ? modes.w[0] : 0, modes.n > 0 ? modes.h[0] : 0,
            modes.n > 0 ? modes.w[modes.n - 1] : 0, modes.n > 0 ? modes.h[modes.n - 1] : 0);
}

/* What SDL says the window IS, published for the editors and said on
 * stderr: the mode from the flags, the client and drawable sizes, the
 * exclusive mode in force, the display, the swap interval read back. */
static void sdl_publish_state(const char *why)
{
    int mode = sdl_read_mode();
    int w = 0, h = 0, dw = 0, dh = 0, mw = 0, mh = 0, mr = 0, vs;
    SDL_DisplayMode m;
    int dk_w = 0, dk_h = 0;
    Uint32 flags = SDL_GetWindowFlags(win);
    SDL_GetWindowSize(win, &w, &h);
    SDL_GL_GetDrawableSize(win, &dw, &dh);
    if (SDL_GetWindowDisplayMode(win, &m) == 0) { mw = m.w; mh = m.h; mr = m.refresh_rate; }
    vs = SDL_GL_GetSwapInterval();
    sl_window_desktop(&dk_w, &dk_h);
    sl_window_publish_state(mode, mode == SL_WINDOW_FULLSCREEN ? mw : w, mode == SL_WINDOW_FULLSCREEN ? mh : h, vs);
    if (why != NULL) {
        /* the exclusive mode is a fact only in FULLSCREEN; elsewhere SDL
         * reports the mode it WOULD use, which is noise */
        if (mode == SL_WINDOW_FULLSCREEN)
            fprintf(stderr, "sightline gfx: display state (%s): mode=%s flags=0x%x client=%dx%d drawable=%dx%d"
                            " fsmode=%dx%d@%d display=%d desktop=%dx%d swap=%d\n",
                    why, sl_window_mode_name(mode), (unsigned) flags, w, h, dw, dh, mw, mh, mr, s_display, dk_w, dk_h, vs);
        else
            fprintf(stderr, "sightline gfx: display state (%s): mode=%s flags=0x%x client=%dx%d drawable=%dx%d"
                            " fsmode=- display=%d desktop=%dx%d swap=%d\n",
                    why, sl_window_mode_name(mode), (unsigned) flags, w, h, dw, dh, s_display, dk_w, dk_h, vs);
    }
}

/* The GL drawable SDL reports against the size the transition asked for.
 * Not redundant with SDL_GetWindowSize: the two are different questions -
 * SDL's own bookkeeping against the client rectangle the OS actually holds
 * (GetClientRect) - and they were MEASURED to disagree (2026-09-20): the
 * parked, never-activated evidence window taken to an exclusive 2560x1440
 * on the 5120x1440 panel had the mode switched and SDL saying 2560x1440
 * while the HWND kept its 5120x1440 client and the drawable with it (an
 * activated player window resizes as asked: measured the same day). A
 * transition whose drawable is not the size asked for is not established,
 * and is restored rather than committed. */
static int sdl_drawable_is(int w, int h)
{
    int dw = 0, dh = 0;
    SDL_GL_GetDrawableSize(win, &dw, &dh);
    if (dw != w || dh != h)
        fprintf(stderr, "sightline gfx: drawable %dx%d is not the %dx%d asked for\n", dw, dh, w, h);
    return dw == w && dh == h;
}

/* One transition to `want` (mode, and the size when one applies). Returns
 * 1 when SDL's read-back matches the target, 0 otherwise - in which case
 * the caller restores. `size_chosen` says the pair came from a request (a
 * fallback taken here is reported, never committed); *out_w / *out_h are
 * the size the mode was asked for (the client, or the exclusive mode). */
static int sdl_transition(int want, int want_w, int want_h, int size_chosen, int aspect,
                          int *out_w, int *out_h, int *fell_back)
{
    int cur = sdl_read_mode();
    int tw = 0, th = 0, ok = 0;
    int d = SDL_GetWindowDisplayIndex(win);
    if (d < 0) d = 0;
    *fell_back = 0;

    if (want == SL_WINDOW_WINDOWED) {
        const struct sl_window_list *wl = sl_window_list_windowed_now();
        int sw = 0, sh = 0, used;
        if (!size_chosen) sl_window_stored_windowed(&sw, &sh);
        else { sw = want_w; sh = want_h; }
        used = sl_window_pick_windowed(wl, sh, s_windowed_h, aspect, &tw, &th);
        if (sh > 0 && !used) {
            *fell_back = 1;
            fprintf(stderr, "sightline gfx: windowed height %d is not one display %d offers -> %dx%d (the window to return to; not rewritten)\n",
                    sh, d, tw, th);
        }
        if (cur != SL_WINDOW_WINDOWED) {
            if (SDL_SetWindowFullscreen(win, 0) != 0)
                fprintf(stderr, "sightline gfx: SDL_SetWindowFullscreen(0) failed: %s\n", SDL_GetError());
        }
        {
            int w = 0, h = 0;
            SDL_GetWindowSize(win, &w, &h);
            if (w != tw || h != th) {
                SDL_SetWindowSize(win, tw, th);
                if (!s_window_pinned)
                    SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED_DISPLAY(d), SDL_WINDOWPOS_CENTERED_DISPLAY(d));
            } else if (cur != SL_WINDOW_WINDOWED && !s_window_pinned) {
                SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED_DISPLAY(d), SDL_WINDOWPOS_CENTERED_DISPLAY(d));
            }
            SDL_GetWindowSize(win, &w, &h);
            ok = sdl_read_mode() == SL_WINDOW_WINDOWED && w == tw && h == th && sdl_drawable_is(tw, th);
        }
    } else if (want == SL_WINDOW_BORDERLESS) {
        SDL_Rect b;
        int dnow;
        if (cur != SL_WINDOW_BORDERLESS) {
            if (SDL_SetWindowFullscreen(win, SDL_WINDOW_FULLSCREEN_DESKTOP) != 0)
                fprintf(stderr, "sightline gfx: SDL_SetWindowFullscreen(DESKTOP) failed: %s\n", SDL_GetError());
        }
        dnow = SDL_GetWindowDisplayIndex(win);
        if (dnow < 0) dnow = d;
        if (SDL_GetDisplayBounds(dnow, &b) != 0) { b.w = 0; b.h = 0; }
        SDL_GetWindowSize(win, &tw, &th);
        ok = sdl_read_mode() == SL_WINDOW_BORDERLESS && tw > 0 && th > 0 && tw == b.w && th == b.h && sdl_drawable_is(tw, th);
    } else {
        const struct sl_window_list *ml = sl_window_list_fullscreen();
        SDL_DisplayMode dm, want_m, got_m;
        int sw = 0, sh = 0, used, dk_w = 0, dk_h = 0;
        if (!size_chosen) sl_window_stored_fullscreen(&sw, &sh);
        else { sw = want_w; sh = want_h; }
        sl_window_desktop(&dk_w, &dk_h);
        if (SDL_GetDesktopDisplayMode(d, &dm) != 0) { dm.w = dk_w; dm.h = dk_h; dm.refresh_rate = 0; dm.format = 0; }
        used = sl_window_pick_fullscreen(ml, sw, sh, dm.w, dm.h, &tw, &th);
        if (sw > 0 && sh > 0 && !used) {
            *fell_back = 1;
            fprintf(stderr, "sightline gfx: fullscreen %dx%d is not a mode display %d offers -> the desktop's %dx%d (not rewritten)\n",
                    sw, sh, d, tw, th);
        }
        want_m.format = dm.format; want_m.w = tw; want_m.h = th; want_m.refresh_rate = dm.refresh_rate; want_m.driverdata = NULL;
        if (SDL_GetClosestDisplayMode(d, &want_m, &got_m) == NULL || got_m.w != tw || got_m.h != th) {
            fprintf(stderr, "sightline gfx: no display mode %dx%d on display %d (%s)\n", tw, th, d, SDL_GetError());
            ok = 0;
        } else {
            /* A MODE CHANGE WHILE ALREADY FULLSCREEN GOES THROUGH WINDOWED.
             * Measured 2026-09-20 (SDL 2.32.10, windows driver, an
             * activated on-screen window as much as the parked one):
             * SDL_SetWindowDisplayMode on a window that is already
             * exclusive switches the panel's mode and updates SDL's own
             * size, but the HWND keeps its old client rectangle - 5120x1440
             * under a 2560x1440 mode, the drawable with it (the read-back
             * below caught it and restored). Leaving exclusive fullscreen
             * first and re-entering it at the new mode - the path that
             * establishes the mode at init - sizes the window as asked. */
            if (cur == SL_WINDOW_FULLSCREEN) {
                SDL_DisplayMode have;
                if (SDL_GetWindowDisplayMode(win, &have) == 0 && (have.w != tw || have.h != th)) {
                    if (SDL_SetWindowFullscreen(win, 0) != 0)
                        fprintf(stderr, "sightline gfx: SDL_SetWindowFullscreen(0) before the mode change failed: %s\n", SDL_GetError());
                    cur = SL_WINDOW_WINDOWED;
                }
            }
            if (SDL_SetWindowDisplayMode(win, &got_m) != 0)
                fprintf(stderr, "sightline gfx: SDL_SetWindowDisplayMode(%dx%d@%d) failed: %s\n", got_m.w, got_m.h, got_m.refresh_rate, SDL_GetError());
            if (cur != SL_WINDOW_FULLSCREEN) {
                if (SDL_SetWindowFullscreen(win, SDL_WINDOW_FULLSCREEN) != 0)
                    fprintf(stderr, "sightline gfx: SDL_SetWindowFullscreen(FULLSCREEN) failed: %s\n", SDL_GetError());
            }
            {
                SDL_DisplayMode m; int w = 0, h = 0;
                SDL_GetWindowSize(win, &w, &h);
                ok = sdl_read_mode() == SL_WINDOW_FULLSCREEN && SDL_GetWindowDisplayMode(win, &m) == 0
                     && m.w == tw && m.h == th && w == tw && h == th && sdl_drawable_is(tw, th);
            }
        }
    }
    *out_w = tw; *out_h = th;
    return ok;
}

static void sdl_apply_display(int at_init)
{
    int aspect = sl_aspect_active() ? sl_aspect_ratio() : SL_ASPECT_4_3;
    int rmode = 0, rw = 0, rh = 0, rvs = 0, req;
    int want_mode, want_w = 0, want_h = 0, size_chosen, vsync_chosen, want_vs;
    int old_mode, old_w = 0, old_h = 0, old_vs;
    int got_w = 0, got_h = 0, fell = 0, ok = 1, vs_ok = 1, vs_now;
    const char *why = NULL;

    if (win == NULL) return;
    req = sl_window_request_take(&rmode, &rw, &rh, &rvs);
    if (!at_init && req == 0 && aspect == s_app_aspect && s_app_mode >= 0)
        return;                                   /* nothing changed */
    sdl_publish_display();

    old_mode = sdl_read_mode();
    old_vs   = SDL_GL_GetSwapInterval();
    SDL_GetWindowSize(win, &old_w, &old_h);
    if (old_mode == SL_WINDOW_WINDOWED && old_w > 0 && old_h > 0) { s_windowed_w = old_w; s_windowed_h = old_h; }

    want_mode    = (req & SL_WINDOW_REQ_MODE)  ? rmode : (at_init ? sl_window_mode() : old_mode);
    size_chosen  = (req & SL_WINDOW_REQ_SIZE)  != 0;
    vsync_chosen = (req & SL_WINDOW_REQ_VSYNC) != 0;
    want_vs      = vsync_chosen ? rvs : (at_init ? sl_vsync() : old_vs);
    if (size_chosen) { want_w = rw; want_h = rh; }
    if (want_mode == SL_WINDOW_BORDERLESS) size_chosen = 0;   /* the desktop owns it */

    /* The B-124 arm's renderbuffers are sized once: no resizing under it. */
    if (sl_BindFramebuffer != NULL && sl_fbo != 0 && (want_mode != old_mode || size_chosen || aspect != s_app_aspect)) {
        fprintf(stderr, "sightline gfx: display change refused under the SL_Z_QUANT offscreen arm (renderbuffers sized once); the fit rule applies\n");
        want_mode = old_mode; size_chosen = 0;
    }
    /* A maximised or minimised window is not resized by the aspect (#45). */
    if (want_mode == SL_WINDOW_WINDOWED && old_mode == SL_WINDOW_WINDOWED && !size_chosen && req == 0
        && (SDL_GetWindowFlags(win) & (SDL_WINDOW_MAXIMIZED | SDL_WINDOW_MINIMIZED))) {
        fprintf(stderr, "sightline gfx: aspect %s selected; the window is fixed (flags 0x%x), the fit rule applies\n",
                sl_aspect_name(aspect), (unsigned) SDL_GetWindowFlags(win));
        s_app_aspect = aspect; s_app_mode = old_mode;
        return;
    }

    /* ---- the mode and the size: transition, verify, else restore ------- */
    if (want_mode != old_mode || size_chosen || at_init || aspect != s_app_aspect) {
        int before_w = old_w, before_h = old_h;
        ok = sdl_transition(want_mode, want_w, want_h, size_chosen, aspect, &got_w, &got_h, &fell);
        if (ok) {
            int now_w = 0, now_h = 0;
            SDL_GetWindowSize(win, &now_w, &now_h);
            if (want_mode == SL_WINDOW_WINDOWED && want_mode == old_mode && !size_chosen && req == 0 && (now_w != before_w || now_h != before_h))
                fprintf(stderr, "sightline gfx: aspect %s -> window %dx%d -> %dx%d (height kept, width = height x %s)\n",
                        sl_aspect_name(aspect), before_w, before_h, now_w, now_h, sl_aspect_name(aspect));
            else if (want_mode == SL_WINDOW_WINDOWED && want_mode == old_mode && !size_chosen && req == 0)
                fprintf(stderr, "sightline gfx: aspect %s -> window %dx%d already fits\n", sl_aspect_name(aspect), now_w, now_h);
            else if (want_mode != SL_WINDOW_WINDOWED && want_mode == old_mode && !size_chosen && req == 0)
                fprintf(stderr, "sightline gfx: aspect %s selected; the window is %s (flags 0x%x), the fit rule applies\n",
                        sl_aspect_name(aspect), sl_window_mode_name(want_mode), (unsigned) SDL_GetWindowFlags(win));
            why = at_init ? "init" : "applied";
        } else {
            int rw2 = 0, rh2 = 0, f2 = 0, restored;
            fprintf(stderr, "sightline gfx: %s %dx%d NOT established (SDL reports mode=%s) - restoring %s %dx%d\n",
                    sl_window_mode_name(want_mode), got_w, got_h, sl_window_mode_name(sdl_read_mode()),
                    sl_window_mode_name(old_mode), old_mode == SL_WINDOW_WINDOWED ? s_windowed_w : old_w,
                    old_mode == SL_WINDOW_WINDOWED ? s_windowed_h : old_h);
            restored = sdl_transition(old_mode, old_mode == SL_WINDOW_WINDOWED ? s_windowed_w : old_w,
                                      old_mode == SL_WINDOW_WINDOWED ? s_windowed_h : old_h,
                                      old_mode != SL_WINDOW_BORDERLESS, aspect, &rw2, &rh2, &f2);
            if (!restored) {
                fprintf(stderr, "sightline gfx: restore failed too - falling back to WINDOWED %dx%d (the launcher's)\n",
                        sl_display_window_width(s_launch_h, aspect), s_launch_h);
                SDL_SetWindowFullscreen(win, 0);
                SDL_SetWindowSize(win, sl_display_window_width(s_launch_h, aspect), s_launch_h);
                if (!s_window_pinned)
                    SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
            }
            why = "restored";
        }
    }

    /* ---- vsync: the live context's swap interval, read back ------------ */
    if (vsync_chosen || at_init) {
        if (SDL_GL_SetSwapInterval(want_vs) != 0)
            fprintf(stderr, "sightline gfx: SDL_GL_SetSwapInterval(%d) refused: %s\n", want_vs, SDL_GetError());
        vs_now = SDL_GL_GetSwapInterval();
        vs_ok = vs_now == want_vs;
        if (!vs_ok) {
            fprintf(stderr, "sightline gfx: vsync %d asked, swap interval read back %d - keeping %d (nothing written)\n",
                    want_vs, vs_now, old_vs);
            SDL_GL_SetSwapInterval(old_vs);
        } else if (vsync_chosen || want_vs != 0) {
            fprintf(stderr, "sightline gfx: vsync %s (swap interval %d read back)\n", want_vs ? "ON" : "OFF", vs_now);
        }
        if (why == NULL) why = at_init ? "init" : "applied";
    }

    /* ---- commit what SDL confirmed, on a request only ------------------ */
    if (req != 0) {
        int now_mode = sdl_read_mode();
        int now_w = 0, now_h = 0;
        SDL_DisplayMode m;
        SDL_GetWindowSize(win, &now_w, &now_h);
        if (now_mode == SL_WINDOW_FULLSCREEN && SDL_GetWindowDisplayMode(win, &m) == 0) { now_w = m.w; now_h = m.h; }
        sl_window_commit(ok ? now_mode : -1, ok && size_chosen && !fell, now_w, now_h,
                         vsync_chosen && vs_ok, SDL_GL_GetSwapInterval());
        if (!ok)
            fprintf(stderr, "sightline gfx: the request was not committed (config.ini unchanged)\n");
    }

    s_app_mode   = sdl_read_mode();
    s_app_aspect = aspect;
    if (s_app_mode == SL_WINDOW_WINDOWED) SDL_GetWindowSize(win, &s_windowed_w, &s_windowed_h);
    sdl_publish_state(why);
}

static void sdl_begin(void)
{
    /* N64 framebuffer is 320x240; the viewport is set to the window for now. */
    int w, h;
    sdl_apply_display(0);                /* #45 / #52: the selection sets the window */
    SDL_GetWindowSize(win, &w, &h);
    /* B-124 arm only: the whole frame - scene, HUD, readbacks - happens
     * inside the 16-bit-depth FBO; sdl_end blits the colour to the window.
     * Re-bound every frame so nothing that unbinds it can leak past a frame. */
    if (sl_BindFramebuffer != NULL && sl_fbo != 0)
        sl_BindFramebuffer(SL_GL_FRAMEBUFFER, sl_fbo);
    glViewport(0, 0, w, h);
    glClearColor(0.05f, 0.06f, 0.09f, 1.0f);
    /* B-125: the stencil plane starts every frame at zero; the redraw path
     * leaves it at zero behind itself, but a frame boundary is the one place
     * that invariant is re-established rather than assumed. */
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

/* Capture the frame we just drew, straight out of GL.
 *
 * Every attempt to photograph this window from OUTSIDE has failed: WSLg
 * refuses root capture, Xvfb comes back uniformly black, and an `import`
 * against the window id produced 42 byte-identical PNGs across frames whose
 * content demonstrably changed - a stale compositor surface, not a render.
 *
 * Reading the framebuffer from inside the process sidesteps the compositor
 * entirely: whatever glReadPixels returns is what GL actually rasterised.
 *
 * Off unless SL_SHOT names a path prefix, so the normal path pays nothing.
 * Output is ROM-derived pixels - it defaults under /tmp and must never be
 * committed (project rule 2).
 */
static void sdl_shot(void)
{
    static const char *prefix;
    static int every, checked, seq, by_read, have_shot;
    static unsigned frame, first, last, shot_at;
    int w, h, y, div;
    unsigned char *px;
    char path[512];
    size_t n;
    const char *s;
    FILE *f;

    if (!checked) {
        const char *e;
        checked = 1;
        prefix = getenv("SL_SHOT");
        e = getenv("SL_SHOT_EVERY");
        every = e ? atoi(e) : 60;
        if (every <= 0) every = 60;
        /* SL_SHOT_FIRST/SL_SHOT_LAST bound the capture to a frame window, so
         * SL_SHOT_EVERY=1 around a one-frame artefact does not have to write
         * a gigabyte of frames before it to get there. */
        e = getenv("SL_SHOT_FIRST");
        first = e ? (unsigned) atoi(e) : 0u;
        e = getenv("SL_SHOT_LAST");
        last = e ? (unsigned) atoi(e) : 0xffffffffu;
        /* SL_SHOT_READ_FIRST/LAST window on the INPUT RECORD INDEX instead.
         *
         * B-051. sl_frames_completed() and sl_record_index() are two different
         * counters and they are not proportional: one run ended with
         * sl_frame=10142 against read=7949, so a window expressed in frames
         * lands somewhere else entirely in record terms. A run's marks file is
         * written in RECORD units (sl_input.c calls sl_run_mark with
         * sl_record_index()), so asking for a marked moment in any other unit
         * is a units bug waiting to happen - and it happened, twice, before
         * this existed. Name the moment in the units the mark was recorded in. */
        e = getenv("SL_SHOT_READ_FIRST");
        if (e != NULL) { by_read = 1; first = (unsigned) atoi(e); }
        e = getenv("SL_SHOT_READ_LAST");
        if (e != NULL) { by_read = 1; last = (unsigned) atoi(e); }
    }
    if (prefix == NULL) return;
    {
        /* Windowed on the shim's VI counter, not a local one: that is the
         * number every other log line in the build is stamped with, so a
         * window read off an instrumentation log lands where it was meant to. */
        extern unsigned sl_frames_completed(void);
        extern unsigned sl_record_index(void);
        unsigned f = by_read ? sl_record_index()
                   : (first != 0u || last != 0xffffffffu)
                        ? sl_frames_completed() : frame;
        frame++;
        if (f < first || f > last) return;
        /* "at least `every` since the last capture", NOT "divisible by
         * `every`".
         *
         * B-051. The read counter does not take every value - it advances by
         * two or more per rendered frame - so an exact-divisibility test can
         * step straight over every multiple of the stride and capture NOTHING,
         * silently, while every other counter in the run reads healthy. That
         * happened here with SL_SHOT_EVERY=100 over a read window: zero files,
         * no error. A window that cannot fire is the same class of fault as a
         * gate that cannot fail. */
        if (have_shot && f - shot_at < (unsigned) every) return;
        have_shot = 1;
        shot_at = f;
    }

    SDL_GetWindowSize(win, &w, &h);
    if (w <= 0 || h <= 0) return;
    px = malloc((size_t) w * (size_t) h * 3);
    if (px == NULL) return;

    /* The back buffer is still current: SDL_GL_SwapWindow has not run yet. */
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px);

    /* Build "<prefix>-NNNNN.ppm" by hand rather than with sprintf.
     *
     * sprintf here is NOT libc's: src/sprintf.c defines one and that is the
     * symbol that links (nm shows `T sprintf` in the game's address range).
     * It is a libultra-oriented implementation that produces nothing
     * natively, so the first version of this silently wrote an empty path and
     * fopen("") returned NULL - no file, no error, no output. fprintf is
     * unaffected because the decomp does not define that one. */
    n = 0;
    s = prefix;
    while (*s != '\0' && n < sizeof path - 16) path[n++] = *s++;
    path[n++] = '-';
    for (div = 10000; div > 0; div /= 10)
        path[n++] = (char) ('0' + (seq / div) % 10);
    path[n++] = '.'; path[n++] = 'p'; path[n++] = 'p'; path[n++] = 'm';
    path[n] = '\0';
    seq++;

    f = fopen(path, "wb");
    if (f != NULL) {
        fprintf(f, "P6\n%d %d\n255\n", w, h);
        /* GL's origin is bottom-left; PPM's is top-left. */
        for (y = h - 1; y >= 0; y--)
            fwrite(px + (size_t) y * (size_t) w * 3, 1, (size_t) w * 3, f);
        fclose(f);
        /* Self-identifying: the filename is a SEQUENCE number, not a frame,
         * so a capture that does not say which moment it is cannot be told
         * from one taken at the wrong moment. Both counters, every time. */
        {
            extern unsigned sl_frames_completed(void);
            extern unsigned sl_record_index(void);
            fprintf(stderr, "sl_shot: %s (%dx%d) at read=%u frame=%u"
                            " (window=%s)\n",
                    path, w, h, sl_record_index(), sl_frames_completed(),
                    by_read ? "read" : "frame");
        }
    } else {
        fprintf(stderr, "sl_shot: cannot open %s\n", path);
    }
    free(px);
}


/* ---- LIVE OWNER BUG MARK: the framebuffer of the marked frame ------------
 *
 * The back buffer, straight out of GL, into a 24-bit BMP at a caller-supplied
 * path. Same read-back sdl_shot uses and for the same reason recorded there:
 * every attempt to photograph this window from OUTSIDE failed (WSLg refuses
 * root capture, Xvfb comes back black, and `import` produced 42 byte-identical
 * PNGs across frames whose content demonstrably changed). Whatever
 * glReadPixels returns is what GL actually rasterised.
 *
 * BMP, not PPM: the owner has to be able to double-click it. The format is
 * bottom-up by definition, which is GL's own row order, so unlike the PPM path
 * there is no flip - only the BGR channel order, which BMP also shares with
 * nothing else here. GL_BGR is not in every <GL/gl.h> this builds against, so
 * the swap is done in the loop rather than asked of the driver.
 *
 * Returns 1 only when a file was written. The caller prints which, and prints
 * "no screenshot" when this returns 0 - a mark that quietly lacks its image
 * would look exactly like a mark whose image was black.
 */
static int sl_mark_shot_bmp(const char *path)
{
    int w = 0, h = 0, x, y, ok = 0;
    unsigned char *px, *row;
    unsigned char hdr[54];
    unsigned rowbytes, datalen, filelen;
    FILE *f;

    if (win == NULL || ctx == NULL || path == NULL) return 0;
    SDL_GetWindowSize(win, &w, &h);
    if (w <= 0 || h <= 0) return 0;

    rowbytes = ((unsigned) w * 3u + 3u) & ~3u;      /* BMP rows are 4-aligned */
    datalen  = rowbytes * (unsigned) h;
    filelen  = 54u + datalen;

    px = malloc((size_t) w * (size_t) h * 3);
    if (px == NULL) return 0;
    row = malloc(rowbytes);
    if (row == NULL) { free(px); return 0; }

    /* The back buffer is still current: SDL_GL_SwapWindow has not run yet. */
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px);

    memset(hdr, 0, sizeof hdr);
    hdr[0] = 'B'; hdr[1] = 'M';
    hdr[2] = (unsigned char) (filelen        & 0xff);
    hdr[3] = (unsigned char) ((filelen >>  8) & 0xff);
    hdr[4] = (unsigned char) ((filelen >> 16) & 0xff);
    hdr[5] = (unsigned char) ((filelen >> 24) & 0xff);
    hdr[10] = 54;                                    /* pixel data offset */
    hdr[14] = 40;                                    /* BITMAPINFOHEADER  */
    hdr[18] = (unsigned char) ( (unsigned) w        & 0xff);
    hdr[19] = (unsigned char) (((unsigned) w >>  8) & 0xff);
    hdr[20] = (unsigned char) (((unsigned) w >> 16) & 0xff);
    hdr[21] = (unsigned char) (((unsigned) w >> 24) & 0xff);
    hdr[22] = (unsigned char) ( (unsigned) h        & 0xff);
    hdr[23] = (unsigned char) (((unsigned) h >>  8) & 0xff);
    hdr[24] = (unsigned char) (((unsigned) h >> 16) & 0xff);
    hdr[25] = (unsigned char) (((unsigned) h >> 24) & 0xff);
    hdr[26] = 1;                                     /* planes  */
    hdr[28] = 24;                                    /* bpp     */
    hdr[34] = (unsigned char) ( datalen        & 0xff);
    hdr[35] = (unsigned char) ((datalen >>  8) & 0xff);
    hdr[36] = (unsigned char) ((datalen >> 16) & 0xff);
    hdr[37] = (unsigned char) ((datalen >> 24) & 0xff);

    f = fopen(path, "wb");
    if (f != NULL) {
        fwrite(hdr, 1, sizeof hdr, f);
        for (y = 0; y < h; y++) {          /* GL row 0 is the BOTTOM row, and
                                            * so is BMP row 0 - no flip */
            const unsigned char *s = px + (size_t) y * (size_t) w * 3;
            memset(row, 0, rowbytes);
            for (x = 0; x < w; x++) {
                row[x * 3 + 0] = s[x * 3 + 2];
                row[x * 3 + 1] = s[x * 3 + 1];
                row[x * 3 + 2] = s[x * 3 + 0];
            }
            fwrite(row, 1, rowbytes, f);
        }
        fclose(f);
        ok = 1;
        fprintf(stderr, "sl_mark: framebuffer %dx%d -> %s\n", w, h, path);
    } else {
        fprintf(stderr, "sl_mark: cannot open %s\n", path);
    }
    free(row);
    free(px);
    return ok;
}

/* ---- LIVE OWNER BUG MARK: what is actually AT the centre pixel -----------
 *
 * The provenance list says which draws could have painted the centre. This
 * says what the centre pixel IS - and those are different diagnoses that a
 * candidate list alone cannot separate:
 *
 *   sky or clear colour at far depth   nothing was drawn here
 *   opaque world colour at mid depth   geometry drew, and drew wrong
 *   blended colour at mid depth        an effect or alpha surface is on top
 *   a depth discontinuity in the 5x5   the centre is on an edge or a seam
 *
 * OBSERVATIONAL, and it has to be. glReadPixels and glGetIntegerv read; they
 * do not draw, do not touch a texture, a shader, a blend mode or the depth
 * buffer, and this runs after the display-list walk has finished, before the
 * swap, so nothing it does can reach the image being read. GL_PACK_ALIGNMENT
 * is a CLIENT read-back parameter - it affects how pixels are unpacked into
 * our buffer and nothing about rasterisation - and the whole-frame BMP path
 * above has been setting it since the feature shipped.
 *
 * BOUNDED ON PURPOSE. One centre pixel plus a 5x5 neighbourhood: 25 colours
 * and 25 depths. Not a framebuffer dump - a dump nobody reads is not evidence.
 */
static int sl_mark_centre_probe(char *out, int n)
{
    int w = 0, h = 0, cx, cy, x0, y0, bw, bh, i, j, at = 0;
    int depth_bits = 0, stencil_bits = 0;
    unsigned char rgba[5 * 5 * 4];
    float dep[5 * 5];
    int have_depth = 0;
    GLenum err;

    if (win == NULL || ctx == NULL || out == NULL || n < 256) return 0;
    SDL_GetWindowSize(win, &w, &h);
    if (w <= 0 || h <= 0) return 0;

    cx = w / 2;
    cy = h / 2;                       /* GL row order: 0 is the BOTTOM row */
    x0 = cx - 2; y0 = cy - 2;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    bw = (x0 + 5 <= w) ? 5 : (w - x0);
    bh = (y0 + 5 <= h) ? 5 : (h - y0);
    if (bw <= 0 || bh <= 0) return 0;

    memset(rgba, 0, sizeof rgba);
    for (i = 0; i < 25; i++) dep[i] = -1.0f;

    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    while (glGetError() != GL_NO_ERROR) { }         /* start from clean */
    glReadPixels(x0, y0, bw, bh, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    err = glGetError();
    if (err != GL_NO_ERROR) return 0;

    /* The bits GL actually GRANTED to whatever framebuffer is bound (the
     * window, or the B-124 arm's 16-bit FBO) - never the requested attribute,
     * which B-123 showed the driver is free to exceed. */
    {   GLint zb = 0, sb = 0;
        glGetIntegerv(GL_DEPTH_BITS, &zb);
        glGetIntegerv(GL_STENCIL_BITS, &sb);
        depth_bits = (int) zb; stencil_bits = (int) sb;
    }
    glReadPixels(x0, y0, bw, bh, GL_DEPTH_COMPONENT, GL_FLOAT, dep);
    have_depth = (glGetError() == GL_NO_ERROR);

#define PCAT(...)  do {                                                       \
        int k_;                                                               \
        if (at >= n - 1) break;                                               \
        k_ = snprintf(out + at, (size_t) (n - at), __VA_ARGS__);              \
        if (k_ > 0) at += k_;                                                 \
        if (at > n - 1) at = n - 1;                                           \
    } while (0)

    PCAT("[centre-pixel]\n"
         "source              glReadPixels on the marked frame's back buffer,\n"
         "                    taken before SDL_GL_SwapWindow and before any\n"
         "                    other read - the SAME buffer the .bmp holds.\n"
         "framebuffer         %dx%d\n"
         "centre-gl           x=%d y=%d   (GL coords: y counts from the BOTTOM,\n"
         "                    which is also the .bmp's row order)\n"
         "centre-image        x=%d y=%d   (top-left origin, for an image viewer)\n",
         w, h, cx, cy, cx, h - 1 - cy);

    {   /* the centre pixel itself, located within the block that was read */
        int ci = (cy - y0) * bw + (cx - x0);
        if (ci < 0 || ci >= bw * bh) ci = 0;
        PCAT("centre-rgba         %02x %02x %02x %02x   (r g b a, 0-255)\n",
             rgba[ci * 4 + 0], rgba[ci * 4 + 1],
             rgba[ci * 4 + 2], rgba[ci * 4 + 3]);
        PCAT("depth-bits          %d   (granted, GL_DEPTH_BITS of the bound framebuffer)\n"
             "stencil-bits        %d   (granted; the B-125 redraw path needs > 0)\n",
             depth_bits, stencil_bits);
        if (have_depth && depth_bits > 0) {
            double eye = 0.0;
            extern int sl_mark_depth_to_eye(double d, double *eye_out);
            PCAT("centre-depth        %.9f   (window depth, 0 = near plane,"
                 " 1 = far)\n", (double) dep[ci]);
            if (dep[ci] >= 0.999999f)
                PCAT("centre-depth-note   AT OR BEYOND THE FAR PLANE. Nothing wrote\n"
                     "                    depth here: this is cleared background,\n"
                     "                    sky, or a draw with depth write off.\n");
            else if (sl_mark_depth_to_eye((double) dep[ci], &eye))
                PCAT("centre-depth-eye    %.2f  (eye-space distance, through the\n"
                     "                    projection in [renderer])\n", eye);
        } else {
            PCAT("centre-depth        UNAVAILABLE (depth bits = %d, read %s)\n",
                 depth_bits, have_depth ? "succeeded" : "FAILED");
        }
    }

    PCAT("\nneighbourhood       %dx%d around the centre, GL order: the FIRST row\n"
         "                    printed is the BOTTOM row. Bounded on purpose -\n"
         "                    enough to see an edge, not a framebuffer dump.\n",
         bw, bh);
    for (j = bh - 1; j >= 0; j--) {
        PCAT("  rgb  ");
        for (i = 0; i < bw; i++) {
            const unsigned char *p = &rgba[(j * bw + i) * 4];
            PCAT("%02x%02x%02x%02x ", p[0], p[1], p[2], p[3]);
        }
        PCAT("%s\n", (j == cy - y0) ? " <- centre row" : "");
    }
    if (have_depth && depth_bits > 0) {
        float dmin = 2.0f, dmax = -1.0f;
        for (j = bh - 1; j >= 0; j--) {
            PCAT("  z    ");
            for (i = 0; i < bw; i++) {
                float d = dep[j * bw + i];
                if (d < dmin) dmin = d;
                if (d > dmax) dmax = d;
                PCAT("%.6f ", (double) d);
            }
            PCAT("%s\n", (j == cy - y0) ? " <- centre row" : "");
        }
        PCAT("  z-range           %.6f .. %.6f  (spread %.6f - a large spread\n"
             "                    means the centre sits on a depth\n"
             "                    discontinuity: an edge, a seam, or a\n"
             "                    silhouette)\n",
             (double) dmin, (double) dmax, (double) (dmax - dmin));
    }
#undef PCAT
    return at;
}

static void sdl_end(void)
{
    /* The MARK, before the swap and before sdl_shot, so the framebuffer it
     * reads back is the one the display-list walk just filled - the same
     * frame the provenance was collected from. A no-op unless F8 armed a
     * capture; see sl_run_mark_full in src/platform/sl_main.c. */
    {
        extern void sl_run_mark_full(int (*shoot)(const char *path),
                                     int (*probe)(char *out, int n));
        sl_run_mark_full(sl_mark_shot_bmp, sl_mark_centre_probe);
    }
    sdl_shot();                       /* before the swap: back buffer is live */
    /* B-124: present the FBO's colour. Blit AFTER every readback above, so
     * the mark/shot instruments read the same rasterization the player sees;
     * bind back to the window so the swap presents it, and sdl_begin re-binds
     * the FBO for the next frame. */
    if (sl_BindFramebuffer != NULL && sl_fbo != 0) {
        sl_BindFramebuffer(SL_GL_READ_FRAMEBUFFER, sl_fbo);
        sl_BindFramebuffer(SL_GL_DRAW_FRAMEBUFFER, 0);
        sl_BlitFramebuffer(0, 0, sl_fbo_w, sl_fbo_h, 0, 0, sl_fbo_w, sl_fbo_h,
                           GL_COLOR_BUFFER_BIT, GL_NEAREST);
        sl_BindFramebuffer(SL_GL_FRAMEBUFFER, 0);
    }
    SDL_GL_SwapWindow(win);
}

static int sdl_poll(void)
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        /* Closing the window, or Alt+F4, is the quit path and the only one.
         * A 0 return here reaches the frame pump as exit(0). */
        if (e.type == SDL_QUIT)
            return 0;

        /* Escape used to return 0 too, which is why it quit the game. It is
         * now handed to the input layer, which turns it into the N64 button
         * the game already uses to leave wherever the player is: START in
         * play and in the watch (bondview2.c:4839), B in the front end
         * (front.c:2315). Key repeats are dropped - holding Escape must not
         * hammer the pause button. */
        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE
            && e.key.repeat == 0) {
            sl_input_live_escape();
            continue;
        }

        /* TAB, the same way and for the same reason: an EVENT, so that holding
         * it cannot hammer START. Tab and Escape are now the only PC keys that
         * open or close the watch - Enter used to, which was the defect. */
        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_TAB
            && e.key.repeat == 0) {
            sl_input_live_start();
            continue;
        }

        /* A click is what takes the pointer. Nothing else does, and nothing
         * here looks at WHERE the click was: SDL only delivers button events
         * for the window that owns the pointer, so arriving at all is the
         * test. See sl_input_live_click. */
        if (e.type == SDL_MOUSEBUTTONDOWN) {
            /* WHICH button and HOW MANY clicks are now passed on. The capture
             * still happens on any button and still ignores where the click
             * was; the two new fields are for the front end's menu confirm,
             * which is left-button only and uses SDL's own `clicks` counter to
             * refuse the second edge of a double click. Both come straight off
             * the event - no timer and no debounce of ours. */
            sl_input_live_click(e.button.button, e.button.clicks);
            continue;
        }

        /* The UP edge, which nothing observed before. It re-arms the next
         * click and clears the mark a menu click leaves, which is what stops
         * the click that started the level from also firing the gun. */
        if (e.type == SDL_MOUSEBUTTONUP) {
            sl_input_live_release(e.button.button);
            continue;
        }

        /* Wheel notches. SDL_MOUSEWHEEL_FLIPPED means the platform already
         * inverted y for "natural" scrolling and expects the app to undo it;
         * unflipping here is what keeps one physical notch upward equal to one
         * menu step upward whatever the desktop is set to. */
        if (e.type == SDL_MOUSEWHEEL) {
            int y = e.wheel.y;
            if (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
                y = -y;
            if (y != 0)
                sl_input_live_wheel(y);
            continue;
        }

        /* Controllers arriving and leaving. This drain is the ONLY place they
         * can be seen: it consumes the whole queue every frame, so an input
         * layer that looked for these itself would always find them already
         * gone. The device list genuinely changes while the game runs - a pad
         * plugged in throughout still drops out and returns during SDL's own
         * startup (measured; see sl_input_live_device_change) - so this is
         * what replaces the probe that used to run exactly once. */
        if (e.type == SDL_CONTROLLERDEVICEADDED
            || e.type == SDL_CONTROLLERDEVICEREMOVED) {
            sl_input_live_device_change();
            continue;
        }

        /* Focus, which nothing observed before this: every event but QUIT and
         * Escape was discarded here, so the input layer could not tell a
         * focused window from an alt-tabbed one and would have held the
         * pointer either way. */
        if (e.type == SDL_WINDOWEVENT) {
            if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
                sl_input_live_focus(0);
            else if (e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED)
                sl_input_live_focus(1);
            /* #52: the size SDL reports after any change - a witness only.
             * Nothing acts on it: the authoritative size is read from the
             * window at the next frame reset (sdl_begin -> glViewport ->
             * sl_gfx_dl.c g_window_vp), which is the one path a mode or
             * resolution change, or a resize the OS makes, all reach. */
            else if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                fprintf(stderr, "sightline gfx: window size changed -> %dx%d (flags 0x%x)\n",
                        (int) e.window.data1, (int) e.window.data2, (unsigned) SDL_GetWindowFlags(win));
        }
    }
    /* Drain first, then sample: SDL_GetKeyboardState and the relative-mouse
     * accumulator are both updated by the pump above, so sampling before it
     * would read the PREVIOUS frame's input. This is once per presented
     * frame, which is the same cadence the recorded stream advances at. */
    sl_input_live_poll();
    return 1;
}

static void sdl_shutdown(void)
{
    sl_input_live_shutdown();          /* drop the pointer grab before SDL_Quit */
    if (ctx) SDL_GL_DeleteContext(ctx);
    if (win) SDL_DestroyWindow(win);
    SDL_Quit();
    ctx = NULL; win = NULL;
}

const struct sl_gfx_backend sl_gfx_sdl = {
    "sdl", sdl_init, sdl_begin, sdl_end, sdl_poll, sdl_shutdown
};
#endif
