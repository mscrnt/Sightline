#ifndef _SL_ROMDBG_H_
#define _SL_ROMDBG_H_

/*
 * SIGHTLINE_ROM_DEBUG - the cartridge's own answer to "what am I looking at?"
 *
 * This exists because object identity by INFERENCE has cost this investigation
 * more time than the instrument costs to build. The player's live position
 * could not be read reliably, so which of twenty tinted-glass panes the owner
 * was looking at had to be guessed, so a glare/material comparison against the
 * cartridge was blocked.
 *
 * The rule this file lives under:
 *
 *     The normal matching ROM stays byte-identical. The debug ROM is knowingly
 *     non-matching and exists solely as an oracle.
 *     Normal ROM proves behaviour. Debug ROM explains state.
 *
 * Everything here is compiled out unless SIGHTLINE_ROM_DEBUG is defined, which
 * only `make trace-debug` does. `make matching` never sees a byte of it.
 *
 * DIVISION OF LABOUR, deliberate: the ROM side does as little as possible. It
 * runs the game's OWN targeting selection - the same per-prop hit test gunfire
 * uses - and writes pointers plus a handful of scalars into a mailbox. It does
 * not format, does not walk models, does not decode display lists. The host
 * (tools/native/rominspect.py) reads the mailbox out of RDRAM and does all of
 * that, where it is cheap and where getting it wrong costs nothing.
 *
 * NOT A SECOND RAYCASTER. sub_GAME_7F04E9BC() is the function a bullet uses to
 * decide whether it hit a prop; slRomDbgSelect() calls exactly that, over
 * exactly the on-screen prop list gunfire walks. A second raycaster is a second
 * instrument that can disagree with the first, which is the failure mode this
 * whole tool exists to delete.
 *
 * ---------------------------------------------------------------------------
 * WHY THE TRIGGER IS A MAILBOX WORD AND NOT A BUTTON
 * ---------------------------------------------------------------------------
 * The intent was "press Select; the normal ROM ignores it, the debug ROM acts
 * on it". Two measured facts rule that out on this platform:
 *
 *  1. The N64 controller has no Select button, and parallel_n64 (the libretro
 *     core the whole harness runs on) has no path to the two spare bits of the
 *     hardware button word. tools/trace/sltrace/emu_libretro.py's BUTTON_MAP
 *     was MEASURED against the running game and covers only real N64 buttons;
 *     its own comment records that libretro id 8 "this core ignores entirely".
 *     A Select press therefore cannot physically reach the ROM as a button.
 *
 *  2. Even if it could, no pad bit is unconsumed. cheat.c:879 reads
 *     joyGetButtons(ANY_BUTTON) / joyGetButtonsPressedThisFrame(ANY_BUTTON)
 *     DURING GAMEPLAY and writes the raw pressed-button word into
 *     g_CurrentPlayer->cheatInputBuffer, advancing cheatInputBufferIndex and
 *     cheatInputCount. Any bit, including the N64's own unused 0x0040, lands in
 *     that buffer and changes player state. lv.c:1217, front.c, options.c,
 *     spectrum.c and ramromreplay.c read ANY_BUTTON too. Search that backs the
 *     absence claim: `grep -rn "joyGetButtons" src/ --include=*.c` - every
 *     0xFFFF / ANY_BUTTON call site is listed in docs/backlog.md.
 *
 * So the reserved control keeps every property that mattered - it is recorded
 * in the input stream like a button, the normal ROM is unaffected, one
 * recording drives both builds - but it is delivered out of band:
 *
 *     InputFrame bit 14 (DBG_SELECT) is logged in the .input stream.
 *     It is NEVER handed to the libretro core.
 *     The host writes it into host_cmd/host_seq below before each frame.
 *
 * That is strictly stronger than an unused button: the normal ROM cannot
 * consume the trigger because the trigger never reaches the console.
 *
 * ---------------------------------------------------------------------------
 * TWO HOSTS DRIVE THIS MAILBOX
 * ---------------------------------------------------------------------------
 *  1. tools/native/rominspect.py replays a finished recording offline.
 *  2. `make trace-record-debug` drives it LIVE while a human plays, and prints
 *     what each press found as it happens. That is not a convenience: for two
 *     sessions the owner pressed the button, the terminal said nothing, and the
 *     misses were only discovered afterwards from a replay - with no way to
 *     tell a press that missed from a press that never registered.
 *
 * Both go through tools/trace/sltrace/romdbg.py, which mirrors this struct
 * exactly once. tools/trace/tests/test_romdbg.py compares that mirror against
 * the offsets annotated below, field by field, so drift is a test failure
 * rather than a plausible-looking wrong answer.
 *
 * Bit 14 is not an arbitrary choice - in the shim's pad encoding
 * (src/platform/sl_ultra_shim.c: byte 0 stick y, byte 1 stick x, bytes 2-3
 * buttons) InputFrame bit 14 is exactly the N64 button word's unused 0x0040,
 * so the same recorded bit means the same thing on the native side.
 *
 * ---------------------------------------------------------------------------
 * CACHE COHERENCY
 * ---------------------------------------------------------------------------
 * The host writes into the emulator's RDRAM behind the CPU's back, and reads it
 * the same way. The mailbox is therefore treated exactly like a DMA buffer:
 * osInvalDCache() before reading the host's half, osWritebackDCache() after
 * writing ours. Without that, host_seq is served from the data cache and the
 * command is never seen, or our record sits dirty in cache and the host reads
 * stale zeroes. Both failure modes look like "the instrument does nothing",
 * which is the kind of silent-null this project has been bitten by five times.
 */

#ifdef SIGHTLINE_ROM_DEBUG

/* No direct <ultra64.h> here: the only SDK-derived things this header uses are
 * the u32/s32/f32 typedefs, and "bondtypes.h" already supplies them (it
 * includes <ultra64.h> itself at src/bondtypes.h:27). The direct include was
 * therefore redundant, and dropping it removes a src/game -> SDK include site.
 * NOTE, plainly: this does NOT sever the dependency - it still arrives
 * transitively through bondtypes.h. It removes a redundant direct edge, not a
 * real coupling; severing that is Phase 1 work at large. */
#include "bondtypes.h"

#define SL_ROMDBG_MAGIC   0x534C4442u   /* 'SLDB' */
#define SL_ROMDBG_VERSION 4u

/* host -> rom */
#define SL_DBG_CMD_NONE     0
#define SL_DBG_CMD_SELECT   1   /* aim + press: this prop becomes the watched one */
#define SL_DBG_CMD_SNAPSHOT 2   /* dump the watched prop's state right now        */
#define SL_DBG_CMD_CLEAR    3   /* forget the watched prop                        */
#define SL_DBG_CMD_FINDTYPE 4   /* host_arg = PROPDEF_*: list every on-screen prop
                                 * of that type, nearest first, and watch the
                                 * nearest. The DIAGNOSTIC, not the identity
                                 * mechanism: SELECT answers "what am I aiming
                                 * at", FINDTYPE answers "is one even here".
                                 * Without it, a SELECT that finds no pane is
                                 * indistinguishable from a level with no panes
                                 * on screen - an instrument that cannot fail
                                 * informatively is barely better than one that
                                 * cannot fail at all. */
#define SL_DBG_CMD_NEAREST  5   /* host_arg = PROPDEF_* (or -1 for anything):
                                 * exactly FINDTYPE's listing, but WITHOUT
                                 * taking over the watched prop. This is what
                                 * the live recorder's aiming aid and its
                                 * miss-detail follow-up use, and the reason it
                                 * is a separate command rather than a flag is
                                 * that a periodic aid must never be able to
                                 * silently replace the identity a deliberate
                                 * SELECT just pinned. */

/* rom -> host: what the record in the mailbox is */
#define SL_DBG_EV_NONE      0
#define SL_DBG_EV_SAMPLE    1   /* routine per-frame sample of the watched prop */
#define SL_DBG_EV_SELECT    2   /* a SELECT resolved and picked a prop          */
#define SL_DBG_EV_MISS      3   /* a SELECT resolved and hit nothing            */
#define SL_DBG_EV_FIND      4   /* a FINDTYPE resolved; hits[] holds the matches */
#define SL_DBG_EV_LOST      5   /* the watched prop's SLOT WAS REUSED - see below */
#define SL_DBG_EV_NEAR      6   /* a NEAREST resolved; hits[] holds the matches
                                 * and the watched prop was NOT touched       */

/* One candidate from the game's own hit list, nearest first. */
typedef struct SlDbgHit {
    u32 prop;        /* PropRecord*    */
    u32 obj;         /* ObjectRecord*  */
    u32 model;       /* Model*         */
    f32 dist;        /* along the shot, the value the game sorted on */
    s32 proptype;    /* PROP_TYPE_*    */
    s32 objtype;     /* PROPDEF_*      */
    /* SELECT: the model hit part, and whether the hit counts against the
     * weapon's shoot-through budget.
     * FINDTYPE: prop->flags, and whether the prop passes the gate at the top
     * of sub_GAME_7F04E9BC - PROPFLAG_ONSCREEN, not RUNTIMEBITFLAG_00001000,
     * not PROPFLAG2_SHOOTTHROUGH. That gate is the reason a prop can be on
     * screen and still never appear in a shot's hit list, so it is reported
     * rather than left to be inferred. */
    s32 hitpart;
    s32 penetrates;
} SlDbgHit;

typedef struct SlRomDbg {
    /* identity - the host refuses to decode anything without these */
    u32 magic;                  /* 0x00 */
    u32 version;                /* 0x04 */
    u32 size;                   /* 0x08 */
    u32 hook_calls;             /* 0x0c known-positive control: this must move */

    /* host -> rom */
    u32 host_seq;               /* 0x10 bumped by the host to issue a command */
    u32 host_cmd;               /* 0x14 */
    u32 host_arg;               /* 0x18 */
    u32 pad1;                   /* 0x1c */

    /* rom -> host */
    u32 rom_seq;                /* 0x20 bumped on every record written */
    u32 rom_ack;                /* 0x24 the host_seq this ROM last acted on */
    s32 frame;                  /* 0x28 currentFrameCounter, the trace's own unit */
    s32 event;                  /* 0x2c SL_DBG_EV_* */

    /* the watched prop */
    u32 watch_prop;             /* 0x30 PropRecord*   - 0 when nothing watched */
    u32 watch_obj;              /* 0x34 ObjectRecord* */
    u32 watch_model;            /* 0x38 Model*        */
    s32 watch_objtype;          /* 0x3c PROPDEF_*     */

    s32 watch_proptype;         /* 0x40 PROP_TYPE_*   */
    s32 watch_propflags;        /* 0x44 prop->flags: PROPFLAG_ENABLED/ONSCREEN */
    s32 watch_onscreen_listed;  /* 0x48 found in g_OnScreenPropList this frame */
    f32 watch_zdepth;           /* 0x4c prop->zDepth */

    f32 watch_pos[3];           /* 0x50 prop->pos           */
    f32 watch_runtime_pos[3];   /* 0x5c obj->runtime_pos    */
    s32 watch_rooms[4];         /* 0x68 prop->rooms[], 0xff terminated */
    s32 watch_stan_room;        /* 0x78 getTileRoom(prop->stan), -1 if no stan */
    u32 watch_objflags;         /* 0x7c obj->flags   */

    u32 watch_objflags2;        /* 0x80 obj->flags2  */
    u32 watch_runtime_bitflags; /* 0x84 */
    f32 watch_damage;           /* 0x88 */
    f32 watch_maxdamage;        /* 0x8c */

    /* tinted glass - meaningful when watch_objtype == PROPDEF_TINTED_GLASS */
    s32 glass_tintdist;         /* 0x90 */
    s32 glass_culldist;         /* 0x94 */
    s32 glass_opacity;          /* 0x98 calculatedopacity, as objTick left it */
    s32 glass_portalnum;        /* 0x9c */

    f32 glass_unk90;            /* 0xa0 */
    s32 portal_ctrl;            /* 0xa4 g_BgPortals[n].controlbytes1, -1 if none */
    s32 portal_room1;           /* 0xa8 */
    s32 portal_room2;           /* 0xac */

    /* the shooter, same frame, so distance is never inferred */
    f32 player_pos[3];          /* 0xb0 getCurrentPlayerProp()->pos */
    f32 aim_origin[3];          /* 0xbc shotdata.gunpos, world space */
    f32 aim_dir[3];             /* 0xc8 shotdata.dir, world space    */
    f32 watch_dist;             /* 0xd4 |runtime_pos - player_pos|   */
    s32 player_room;            /* 0xd8 */
    s32 weapon;                 /* 0xdc ITEM_IDS in the right hand   */

    s32 onscreen_count;         /* 0xe0 length of g_OnScreenPropList */
    s32 nhits;                  /* 0xe4 candidates from the last SELECT */
    /* Identity, pinned at selection time. A PropRecord pointer alone is NOT an
     * identity: prop slots are freed and reused, and the back-pointer check
     * (obj->prop == prop) passes just as happily for the new occupant.
     * MEASURED: a pane watched in facility-props was still being sampled
     * thousands of frames later as objtype 11, at the same address, reading
     * entirely healthy. That is object identity by inference reappearing
     * inside the tool built to delete it. So the obj pointer and PROPDEF at
     * selection are stored, and a sample that disagrees with either reports
     * SL_DBG_EV_LOST and stops watching rather than publishing a lie. */
    u32 watch_obj_sel;          /* 0xe8 ObjectRecord* when selected */
    s32 watch_objtype_sel;      /* 0xec PROPDEF_* when selected     */

    /* IS THERE A CROSSHAIR AT ALL, this frame.
     *
     * Published unconditionally, because it is what separates the two ways a
     * SELECT can come back empty, and the owner cannot act on the difference
     * unless the instrument states it: "I was aiming at the wrong place" is a
     * different mistake from "I was not aiming, so there was nothing to aim
     * with". gunDrawSight() (gunfire.c:6276) draws the crosshair when and only
     * when `gunsightmode == 0 && !mpmenuon`, so those two words ARE the
     * on-screen crosshair - not a proxy for it.
     *
     * sight_mode is a mask of GUNSIGHTREASON_* (bondconstants.h:2866):
     *   0x02 NOTAIMING   0x04 NOCONTROL (cutscene/death)   0x10 DAMAGE
     * so the host can name the reason rather than only its existence. */
    s32 sight_mode;             /* 0xf0 g_CurrentPlayer->gunsightmode */
    s32 sight_mp_menu;          /* 0xf4 g_CurrentPlayer->mpmenuon     */

    SlDbgHit hits[10];          /* 0xf8, 0x20 each -> ends at 0x238 */
} SlRomDbg;

extern SlRomDbg g_SlRomDbg;

/* Called once per frame from lv.c, immediately after chraiCheckUseHeldItems(),
 * so the view matrices are in the state a real shot on this frame would see and
 * objTick() has already written this frame's calculatedopacity. */
void slRomDbgTick(void);

#endif /* SIGHTLINE_ROM_DEBUG */
#endif /* _SL_ROMDBG_H_ */
