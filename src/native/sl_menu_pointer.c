/**
 * sl_menu_pointer.c - the native absolute pointer, driving the front end's
 * OWN cursor.
 *
 * THE FINDING THIS FILE IS BUILT ON. GoldenEye's front end is already a
 * pointer menu, and has been since 1997. There is exactly one cursor:
 *
 *     f32 cursor_h_pos, cursor_v_pos;              src/game/front.c:288-289
 *
 * every front-end screen hit-tests it for itself, and the highlight each
 * screen draws is derived from that hit test, not from a stored index:
 *
 *     file select      folder bounding boxes projected to 2D, then the COPY
 *                      and ERASE bounds        front.c:2494-2497, :2536-2547
 *     mode select      cursor_v_pos thresholds           front.c:2943, :2952
 *     mission select   cursor_xpos_table_mission_select /
 *                      cursor_ypos_table_mission_select  front.c:3271, :3285
 *     difficulty       cursor_v_pos thresholds     front.c:3560, :3564, :3568
 *     briefing         the START / NEXT / PREVIOUS tabs  front.c:1296, :1340,
 *                                                        :1391
 *
 * and the ONE place that cursor moves is frontUpdateControlStickPosition
 * (front.c:1148), which integrates the N64 stick into it.
 *
 * So a native mouse does not need hover state, a hit test, a hitbox table or a
 * highlight of its own, and it must not have one: this file puts the game's
 * own cursor where the pointer is, and every consequence - which item lights
 * up, which entries are locked, what A does - stays exactly where Rare left
 * it. A second selection model would be a second source of truth for something
 * the game already decides.
 *
 * WHY IT LIVES IN src/native. Three facts have to meet, and they belong to
 * three different layers:
 *
 *   the pointer       src/platform/sl_input.c owns the device: position in
 *                     window pixels, window size, a motion serial, the click
 *                     edge. It knows nothing about menus.
 *   the presentation  src/gfx/sl_gfx_dl.c owns where in the window the game
 *                     image lands - sl_gfx_present_rect, derived from the very
 *                     viewport the 2D ortho is stretched across.
 *   the logical space src/fr.c owns the front end's coordinate system, and the
 *                     front end declares it at front.c:8638 as 440x330.
 *
 * src/platform compiles against the HOST headers and physically cannot see a
 * game struct (see the header of sl_game_query.c); src/game must not include
 * from src/gfx or src/platform at all. src/native is the one place compiled
 * with the game include path that is allowed to call outward, which is why the
 * mapping is here and why the seam in front.c is a single guarded call.
 *
 * NOT hardcoded anywhere below: no window size, no 440, no 330, no menu
 * rectangle. The window comes from the platform, the presented rectangle from
 * the renderer, and the logical rectangle from the game's own accessors, so a
 * resize, a different window size, or a future letterbox all follow with no
 * edit here.
 */
#ifndef __sgi

#include <ultra64.h>
#include <bondgame.h>
#include <boss.h>
#include "bondview.h"
#include "front.h"
#include "player.h"

/* The front end's own accessor for current_menu (front.c:8592). Declared here
 * because front.h does not declare it and this file has no business editing a
 * decomp header to add a prototype. */
extern MENU get_currentmenu(void);

/* THE GAME'S OWN "a recorded demo is driving me" FLAG (ramromreplay.c:94).
 * Set by replay_recorded_ramrom_at_address (:571), cleared at :484, and read
 * back by the game itself at :133. A plain non-static s32, read here and never
 * written - the same shape as every other query this layer makes.
 *
 * Why it is a gate: the front end reaches its attract loop on its own
 * (front.c:8215 select_ramrom_to_play), and while a demo is running the game
 * is being driven by recorded controller samples. A pointer that moved the
 * menu cursor underneath that would be perturbing a replay, which is the one
 * thing the native input layer must never do. The stage check below already
 * covers demos that have reached a level; this covers the rest. */
extern s32 g_ramromPlayBackFlag;

/* The platform's pointer snapshot (src/platform/sl_input.h). Declared rather
 * than included, exactly as sl_input.c declares the queries in
 * sl_game_query.c: that header is compiled against the host include tree and
 * this file is compiled against the N64 one. */
extern int sl_input_pointer_get(int *x, int *y, int *win_w, int *win_h,
                                unsigned int *motion_serial);

/* The renderer's presented rectangle (src/gfx/sl_gfx.h), same reasoning. */
extern int sl_gfx_present_rect(int win_h, int *x, int *y, int *w, int *h);

/* Is the MOUSE the thing currently pointing? Used for ONE decision - see
 * "SCREEN ENTRY" in sl_menu_pointer_apply. */
extern int sl_input_pointer_owns(void);

/* The serial this file last acted on. See "OWNERSHIP" below. */
static unsigned int s_last_motion;
static int          s_have_motion;

/* The menu this file last saw, so a CHANGE can be noticed. -2 because
 * MENU_INVALID is -1 and is a menu the front end really reports. */
static s32          s_last_menu = -2;

/**
 * Which front-end screens the pointer drives, and therefore which ones a left
 * click confirms into. The PLATFORM asks this - it must not carry a list of
 * menus - and it is the only place the set is written down.
 *
 * The set is the vertical path to a level and nothing else:
 *
 *   MENU_FILE_SELECT     pick a save folder; also its COPY and ERASE icons,
 *                        which are ordinary cursor-bounded items on the same
 *                        screen (front.c:2536-2547)
 *   MENU_MODE_SELECT     Single Player / Multi / Cheats
 *   MENU_MISSION_SELECT  the mission grid
 *   MENU_DIFFICULTY      Agent / Secret Agent / 00 Agent
 *   MENU_BRIEFING        NOT horizontal broadening. Choosing a difficulty
 *                        leaves the player on the briefing page
 *                        (front.c:3613 frontChangeMenu(MENU_BRIEFING)), and
 *                        the level starts only from its START tab
 *                        (front.c:3995-3999). Without this entry the
 *                        mouse-only path stops one screen short of gameplay.
 *   MENU_MISSION_FAILED  the debrief wallet, page 1 - and despite the name it
 *                        is the screen shown after EVERY solo mission, won or
 *                        lost. front.c:8932 changes to it unconditionally when
 *                        MENU_RUN_STAGE ends for a non-Cuba solo stage; the
 *                        page itself prints COMPLETED or FAILED from
 *                        frontCompleteAllObjectivesAliveSuccess
 *                        (front.c:7301-7311). Its PREVIOUS tab is the way back
 *                        to Mission Select (front.c:7259).
 *   MENU_MISSION_COMPLETE the debrief wallet, page 2 - the time and accuracy
 *                        page, reached by page 1's NEXT tab (front.c:7256).
 *
 *                        Both are the same kind of screen as the briefing and
 *                        were built the same way: each hit-tests this cursor
 *                        through frontCheckCursorOnPreviousTab /
 *                        frontCheckCursorOnNextTab (front.c:7213, :7220,
 *                        :7400, :7406), each calls
 *                        frontUpdateControlStickPosition (front.c:7253, :7440)
 *                        so this file's hook already runs on them, and each
 *                        ends its constructor with frontDrawCursor
 *                        (front.c:7360, :7713). Nothing was missing but the
 *                        classification: without these two entries the front
 *                        end reported "no cursor menu" for the whole
 *                        post-mission path, the platform dropped to SKIP,
 *                        read_pointer was never sampled and Rare's own cursor
 *                        sat wherever the last screen left it while the mouse
 *                        moved over a live, drawn cursor that would not
 *                        follow it.
 *   MENU_CHEAT           the cheat menu. The same kind of screen again, built
 *                        the same way: interface_menu15_cheat hit-tests this
 *                        cursor against its row bands (cursor_v_pos >=
 *                        i*0x14 + 0x35, the right column at cursor_h_pos >=
 *                        0xDC) and the PREVIOUS tab through
 *                        frontCheckCursorOnPreviousTab, toggles the row under
 *                        it on the A edge, calls
 *                        frontUpdateControlStickPosition so this file's hook
 *                        runs on it, and its constructor ends with
 *                        frontDrawCursor. It has no latch and no step
 *                        threshold of its own - the cursor IS the selection.
 *
 *                        Owner-reported 2026-09-16 on the full-campaign demo:
 *                        "mouse doesn't work in the cheat menu". MEASURED on
 *                        the normal build (scratch save with every cheat
 *                        earned) and the demo build alike, same driver as the
 *                        Multiplayer check: with the page up, two real left
 *                        clicks produced ZERO N64 A edges (SL_INPUT_DEBUG
 *                        never showed button=8000 after menu 21 came up),
 *                        the pointer never moved the highlight, while wheel
 *                        notches and the arrow keys still produced their
 *                        stick pulses - so the axis and click plumbing were
 *                        alive and the page was simply not in this set: the
 *                        platform answered SL_PTR_SKIP, read_pointer was
 *                        never sampled, and sl_input_live_click returned
 *                        before arming the confirm. Not demo-specific; the
 *                        demo merely made the page reachable on a clean
 *                        save. Fixed where the set is written down, exactly
 *                        as MENU_MISSION_FAILED / _COMPLETE were.
 *
 * Everything else in the front end - the logos, the legal screen, the gun
 * barrel, multiplayer setup, 007 options, the cast - is left exactly as it
 * was. They are reached by keyboard, pad or their existing any-button skip,
 * and a click neither moves their cursor nor confirms into them. Widening
 * the set later is one line here; no other file changes.
 *
 * get_currentmenu is the game's own accessor for current_menu, so this reads
 * the same value the front end dispatches on (front.c:8735).
 */
s32 sl_game_pointer_menu_active(void)
{
    if (bossGetStageNum() != LEVELID_TITLE)
        return 0;

    /* Never touch a replay. See g_ramromPlayBackFlag. */
    if (g_ramromPlayBackFlag != 0)
        return 0;

    switch (get_currentmenu())
    {
    case MENU_FILE_SELECT:
    case MENU_MODE_SELECT:
    case MENU_MISSION_SELECT:
    case MENU_DIFFICULTY:
    case MENU_BRIEFING:
    case MENU_MISSION_FAILED:
    case MENU_MISSION_COMPLETE:
    case MENU_CHEAT:
        return 1;
    default:
        return 0;
    }
}

/**
 * Which front-end screens a LEFT CLICK advances, which is a wider set than the
 * one above and a different question.
 *
 * The boot chain - legal screen, Nintendo, Rareware, the gun barrel, the
 * GoldenEye logo, the cast roll - is not a pointer menu and gets no hover, no
 * mapping and no cursor write: none of those screens even calls
 * frontUpdateControlStickPosition, so sl_menu_pointer_apply cannot run there.
 * What they DO have is one shared skip path, the same one every physical
 * button already uses:
 *
 *     legal      front.c:1534   joyGetButtonsPressedThisFrame(P1, ANY_BUTTON)
 *     nintendo   front.c:1763   the same
 *     rareware   front.c:1950   the same
 *     gun barrel front.c:1998   the same
 *     GE logo    front.c:2071, :2077   the same
 *     cast       front.c:8220   joyGetButtonsPressedThisFrame(P1, 0xFFFF)
 *
 * So the click needs no new code at all: it already becomes one N64 A edge,
 * and an A edge is a button press. Nothing is duplicated and nothing about
 * keyboard or pad skipping changes.
 *
 * ONE CLICK, ONE SCREEN. The edge is the guarantee, and it is the game's own:
 * joyConsumeSamples (src/joy.c:395) ORs `buttons1 & ~buttons2` across the
 * sample ring, so a button held across several samples contributes exactly one
 * rising edge, in the frame the 0->1 transition falls in, and none afterwards.
 * The platform holds the synthesised A for three polls for an unrelated
 * latching reason (see SL_CONFIRM_HOLD_POLLS), and that hold is still one
 * rising edge - so the second, third and fourth screens of the boot chain see
 * a HELD button, not a pressed one, and do not advance. Holding the physical
 * button is the same story: SDL raises one SDL_MOUSEBUTTONDOWN per press.
 *
 * MENU_SWITCH_SCREENS is deliberately absent. It is the one-or-two-frame
 * transition the front end passes through between screens (front.c:8602), and
 * a click landing inside it is dropped rather than applied to whichever screen
 * happens to come up - which is the safe direction for a cascade.
 *
 * Attract playback is unaffected: a click on the cast roll does what any button
 * does there and goes to file select (front.c:8220-8223). Once a RAMROM demo is
 * actually running the stage is no longer LEVELID_TITLE and both of these
 * functions return 0.
 */
s32 sl_game_click_advance_active(void)
{
    if (bossGetStageNum() != LEVELID_TITLE)
        return 0;

    /* A click must not skip a screen out from under a replay either. */
    if (g_ramromPlayBackFlag != 0)
        return 0;

    if (sl_game_pointer_menu_active())
        return 1;

    switch (get_currentmenu())
    {
    case MENU_LEGAL_SCREEN:
    case MENU_NINTENDO_LOGO:
    case MENU_RAREWARE_LOGO:
    case MENU_EYE_INTRO:
    case MENU_GOLDENEYE_LOGO:
    case MENU_DISPLAY_CAST:
        return 1;
    default:
        return 0;
    }
}

/**
 * Put the front end's cursor under the pointer, if the pointer has moved.
 *
 * Called from the HEAD of frontUpdateControlStickPosition, so the stick's delta
 * integrates on top of the pointer position and, more importantly, the clamps
 * at the end of that function bound this assignment exactly as they bound the
 * stick. Nothing here carries a margin constant of its own.
 *
 * OWNERSHIP, entire: act only when the platform's motion serial CHANGES. A
 * stationary pointer republishes the same serial every frame and this returns
 * without touching anything, so a selection the keyboard or the pad has just
 * moved stays where they put it. The first pixel of real motion advances the
 * serial and the pointer takes the cursor back. There is no arbitration
 * framework, no timer and no "device mode" - one unsigned int.
 *
 * SCREEN ENTRY is the ONE exception, and it is why s_last_menu exists.
 * GoldenEye RE-PLACES its cursor on every menu transition - a plain assignment
 * in Rare's own code, not anything native:
 *
 *     setCursorPOSforMode            front.c:3080, called at :2597, :3448
 *     set_cursor_to_stage_solo       front.c:3453, called at :3006, :3648
 *     set_cursor_pos_difficulty      front.c:3694, called at :3437, :3442
 *     frontSetCursorPositionToNextTab front.c:1376, called at :3635, :3641
 *
 * all of which run AFTER frontChangeMenu on the transition frame. With a
 * stationary mouse the per-frame rule above then correctly declines to
 * override it, so the cursor sat where Rare put it - near the middle of the
 * screen - while the physical pointer was somewhere else entirely, until the
 * next twitch snapped it across. That is the owner's "it shouldn't auto center
 * in the menus, but keep the last mouse position", reported 2026-09-07.
 *
 * So on the first frame of a NEW pointer menu the position is re-asserted once
 * even though the pointer has not moved. Three things this deliberately is not:
 *
 *   - it is not a weakening of the per-frame rule. Within one screen a
 *     stationary mouse still never fights keyboard or pad navigation; the
 *     exception is entry only, and s_last_menu is what bounds it to that.
 *   - it is not unconditional. It requires the MOUSE to be the thing pointing
 *     (sl_input_pointer_owns), which is false until the mouse has actually
 *     moved and false again the moment the keyboard or pad moves the cursor.
 *     A pad player therefore still gets Rare's placement, exactly as before.
 *   - it does NOT touch Rare's reset, and it does not move the physical
 *     pointer. Nothing in this tree calls SDL_WarpMouseInWindow (grep -rn
 *     SDL_WarpMouse src/ finds only tools/trace/slinput/slinput.c, the
 *     mupen64plus replay plugin, which is not linked into sightline.exe).
 *     The OS pointer stays where the player left it; the GAME's cursor is
 *     moved to agree with it.
 *
 * The re-assert lands one frame after the reset, because the interface
 * functions hit-test the cursor and only then call frontUpdateControlStickPosition
 * (e.g. front.c:3576 tests, :3602 updates). So the entry frame still highlights
 * from Rare's position and the frame after highlights from the pointer - the
 * same one-frame latency the stick has always had.
 *
 * DETERMINISM. sl_input_pointer_get returns 0 unless live input is running
 * with a real window, so trace replay, headless runs and RAMROM/demo playback
 * never reach the assignment. The idle timer the front end uses to fall back
 * to the attract loop (front.c:2381) is deliberately NOT reset here: it counts
 * button presses and stick deflection, and neither is synthesised by moving a
 * pointer, so the attract sequence keeps Rare's timing.
 */
void sl_menu_pointer_apply(void)
{
    int px, py, ww, wh;
    int rx, ry, rw, rh;
    unsigned int motion;
    f32 u, v;
    f32 left, top, width, height;
    s32 menu_now;
    int entered, moved;

    /* Tracked before the pointer check, and unconditionally, so that a screen
     * the pointer does NOT drive still counts as having been left. Otherwise
     * stepping out to a non-pointer menu and back would not read as an entry. */
    menu_now = (s32) get_currentmenu();
    entered = (menu_now != s_last_menu);
    s_last_menu = menu_now;

    if (!sl_input_pointer_get(&px, &py, &ww, &wh, &motion))
    {
        /* No pointer this frame - a menu that is not in the set, no focus, or
         * the pointer captured for play. Drop the history so that the next
         * sample is a fresh baseline rather than a phantom jump. */
        s_have_motion = 0;
        return;
    }

    if (!s_have_motion)
    {
        /* First sample is a baseline, not a move. It is ALSO the normal state
         * on the first frame of a new menu, because the transition passes
         * through screens this layer does not sample - which is the second
         * reason entry needed handling of its own rather than just the serial. */
        s_have_motion = 1;
        moved = 0;
    }
    else
    {
        moved = (motion != s_last_motion);
    }
    s_last_motion = motion;

    /* Stationary: whoever moved the cursor last keeps it - unless this is the
     * first frame of a new menu AND the mouse is the thing pointing, in which
     * case the pointer's position is re-asserted over Rare's reset. */
    if (!moved && !(entered && sl_input_pointer_owns()))
        return;

    /* WINDOW PIXELS -> the presented image. The renderer's own rectangle, not
     * a second copy of its scaling: it is the GL viewport the 2D ortho is
     * stretched across, so it already accounts for the window size and would
     * account for a letterbox if one is ever added. */
    if (!sl_gfx_present_rect(wh, &rx, &ry, &rw, &rh))
        return;
    if (rw <= 0 || rh <= 0)
        return;

    u = (f32) (px - rx) / (f32) rw;
    v = (f32) (py - ry) / (f32) rh;

    /* Outside the presented image is not over a menu item. Leave the cursor
     * where it was rather than clamping it to an edge, which would drag the
     * highlight along a border as the pointer left the window. */
    if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f)
        return;

    /* -> the GAME's logical menu space. Read from the game, every frame: the
     * front end sets it to 440x330 at front.c:8638 and the level path sets it
     * to 320x240, and these four accessors are what viSetViewSize/Position
     * write through (src/fr.c). Nothing is assumed about either. */
    left   = getPlayer_c_screenleft();
    top    = getPlayer_c_screentop();
    width  = getPlayer_c_screenwidth();
    height = getPlayer_c_screenheight();
    if (width <= 0.0f || height <= 0.0f)
        return;

    /* NO CLAMP HERE, and that is the point of where the seam sits. This runs at
     * the TOP of frontUpdateControlStickPosition, so the game's own margins
     * (front.c:1198-1219 - the 20-unit inset that keeps the cursor off the very
     * edge, which the tab hit tests depend on) are applied to this assignment a
     * few lines later, by the same code that applies them to the stick. There
     * is no second copy of those numbers anywhere. */
    cursor_h_pos = left + u * width;
    cursor_v_pos = top  + v * height;

}

#endif /* !__sgi */
