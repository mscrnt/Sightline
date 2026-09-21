#include <ultra64.h>
#include <math.h>
#include <os_extension.h>
#include <PR/libaudio.h>
#include <assets/font_dl.h>
#include <deb.h>
#include <memp.h>
#include <music.h>
#include <tlb_manage.h>
#include <fr.h>
#include <snd.h>
#include <ramrom.h>
#include <random.h>
#include <joy.h>
#include <token.h>
#include "debugmenu_handler.h"
#include "lv.h"
#include "language.h"
#include "initcheattext.h"
#include "front.h"
#include "bondinv.h"
#include "player.h"
#include "propobj.h"
#include "cleanup_objects.h"
#include "explosion.h"
#include "chrai.h"
#include "mp_music.h"
#include "initunk_005520.h"
#include "initmttex.h"
#include "glass.h"
#include "image_bank.h"
#include "textrelated.h"
#include "initmenus.h"
#include "cheat.h"
#include "bg.h"
#include "objective.h"
#include "mpmenu.h"
#include "vtxstore.h"
#include "initunk_005450.h"
#include "initobjects.h"
#include "initguards.h"
#include "prop.h"
#include "initexplosioncasing.h"
#include "alloc_window_pieces.h"
#include "initunk_007290.h"
#include "initcheattext.h"
#include "initpathtablelinks.h"
#include "ejectedcartridges.h"
#include "inititemslots.h"
#include "initBondDATA.h"
#include "bondview.h"
#include "bondview_r.h"
#include "initBondDATAdefaults.h"
#include "viewport.h"
#include "stan.h"
#include "gun.h"
#include "debug_camera.h"
#include "mp_music.h"
#include "bgroomtrans.h"
#include "unk_092E50.h"
#include "frametiming.h"
#include "chr.h"
#include "sl_romdbg.h"   /* SIGHTLINE_ROM_DEBUG only; empty otherwise */

// bss
//CODE.bss:8008C260
u32 *ptr_font_DL;
//CODE.bss:8008C264
s32 dword_CODE_bss_8008C264;
s32 dword_CODE_bss_8008C268;
s32 dword_CODE_bss_8008C26C;
//CODE.bss:8008C270
char ramrom_data_target[0x380];
//CODE.bss:8008C5F0
s32 record_slot_num;
//CODE.bss:8008C5F4
u8 * address_demo_loaded;
//CODE.bss:8008C5F8
s32 dword_CODE_bss_8008C5F8;


// data
//D:80048360
s32 lvl_c_debug_notice_list = 0;
//D:80048364
s32 g_CurrentStageToLoad = 0;
//D:80048368
f32 D_80048368 = 1.0;
//D:8004836C
s32 musictrack1_playing = 0;
//D:80048370
s32 g_ControlsLockedFlag = 0;
//D:80048374
s32 g_ClockTimer = 0;


#if defined (BUGFIX_R1)
// addresses updated, per build\ge007.j.map
// 800483a8
f32 g_JP_GlobalTimerDelta = 0;
// 800483ac
s32 g_GlobalTimer = 0;
// 800483b0
s32 D_80048380 = 0;
// 800483b4
f32 g_GlobalTimerDelta = 0;
#else
//D:80048378
f32 g_GlobalTimerDelta = 0;
//D:8004837C
s32 g_GlobalTimer = 0;
//D:80048380
s32 D_80048380 = 0;
//D:80048384
#endif
/*
* Selected difficulty mode.
* 0x80048384
*/
#ifdef SL_DIRECT_BOOT_DIFFICULTY
// Sightline: the difficulty gameplay and objective counts actually read.
//
// set_selected_difficulty() writes the MENU's variable; front.c copies it here
// when you confirm mission select, which direct boot never reaches. Calling
// lvlSetSelectedDifficulty() from boss.c instead HANGS the game at boot - the
// frame counter never advances - so this uses the initialiser, the same
// mechanism that makes SL_DIRECT_BOOT_LEVEL work: in place before the first
// instruction, with no call ordering to get wrong.
//
// Undefined by default, so the matching build is unaffected.
s32 g_SelectedDifficulty = SL_DIRECT_BOOT_DIFFICULTY;
#else
s32 g_SelectedDifficulty = DIFFICULTY_AGENT;
#endif

//D:80048388
s32 D_80048388 = 0;
//D:8004838C
s32 D_8004838C = 0;
//D:80048390
s32 D_80048390 = 0;
//D:80048394
s32 D_80048394 = 0;

/**
 * Address 0x80048398.
 */
s32 g_MpTime = 0x8CA0;

/**
 * Address 0x8004839C.
 */
s32 g_MpPoint = 0xA;

/**
 * Address 0x800483A0.
 */
ALSoundState * g_MpSoundStateRelated = NULL;

/**
 * Address 0x800483A4.
 */
f32 g_CurrentMultiPlayerSec = 0.0;

//D:800483A8
s32 D_800483A8 = 0;

/**
 * Address 0x800483AC.
 */
f32 g_CurrentMultiPlayerMin = 0.0;

//D:800483B0
s32 D_800483B0 = 0;

/**
 * Address 0x800483B4.
 */
f32 g_StageTimeSec = 0;

//D:800483B8
s32 D_800483B8 = 0;

/**
 * Power on time in seconds.
 * Address 0x800483BC.
 */
f32 g_PowerOnTimeSec = 0;

/**
 * Debug variable, seems to track whether user input has changed since
 * the last time the method was entered.
 *
 * Addres 0x800483C0.
 */
s32 D_800483C0 = 1;

//D:800483C4
s32 D_800483C4 = 0xFFFFFFFF;

//D:800483C8
struct LvlMpUnknown *D_800483C8 = NULL;

/**
* Debug variable, something to do with portals.
* Address 0x800483CC.
*/
s32 g_DebugPortalIndex = 0;

/**
 * Input buffer used in debug portal method.
 * Address 0x800483D0.
 */
s32 g_DebugPortalsInputBuffer = 0;
s32 g_DebugPortalsInputBuffer1 = 0;
s32 g_DebugPortalsInputBuffer2 = 0;
s32 g_DebugPortalsInputBuffer3 = 0;
s32 g_DebugPortalsInputBuffer4 = 0;

extern s32 g_DebugPortalsInputBufferSource1;
extern s32 g_DebugPortalsInputBufferSource2;
extern s32 g_DebugPortalsInputBufferSource3;
extern s32 g_DebugPortalsInputBufferSource4;

#pragma weak g_DebugPortalsInputBufferSource1 = g_DebugPortalsInputBuffer1
#pragma weak g_DebugPortalsInputBufferSource2 = g_DebugPortalsInputBuffer2
#pragma weak g_DebugPortalsInputBufferSource3 = g_DebugPortalsInputBuffer3
#pragma weak g_DebugPortalsInputBufferSource4 = g_DebugPortalsInputBuffer4

/**
 * Something debug related in the MP manage method.
 * Index to play sound effect.
 * Address 0x800483E4.
 */
s16 g_DebugMpGameSoundFxIndex = 0;

// unused address padding
s16 D_800483E6 = 0;


extern u8* _fontdlSegmentRomStart;
extern u8* _fontdlSegmentRomEnd;


// forward declarations

Gfx * lvlPortalDebug7F0BDF10(Gfx * arg0);

// end forward declarations

s32 sub_GAME_7F0BD8F0(void)
{
    return D_800483C0;
}

void sub_GAME_7F0BD8FC(s32 arg0)
{
    D_800483C0 = arg0;
}

void lvInit(void)
{
    s32 size;

    debTryAdd(&lvl_c_debug_notice_list, "lv_c_debug");
    size = (s32)&_fontdlSegmentRomEnd - (s32)&_fontdlSegmentRomStart;
    lvl_c_debug_notice_list = 1;
    ptr_font_DL = mempAllocBytesInBank(size, MEMPOOL_PERMANENT);
    romCopy(ptr_font_DL, &_fontdlSegmentRomStart, size);
}

/**
 * Unreferenced.
 */
void lvlPlayRandomMusicTrack1(void)
{
    musictrack1_playing = randomGetNext() % 0x3dU + M_INTRO;
    musicTrack1Play(musictrack1_playing);
}

void lvlPlayMusicTrack1(MUSIC_TRACKS track)
{
    musictrack1_playing = track;
    musicTrack1Play(musictrack1_playing);
}

void lvlMusicAppendPlaySoloDeathShort(void)
{
    musictrack1_playing = (musictrack1_playing + M_SHORT_SOLO_DEATH) % NUM_MUSIC_TRACKS;

    if (musictrack1_playing == M_NONE)
    {
        musictrack1_playing = M_SHORT_SOLO_DEATH;
    }

    musicTrack1Play(musictrack1_playing);
}

void lvlMusicAppendPlayEndTheme(void)
{
    musictrack1_playing = (musictrack1_playing + M_END_SOMETHING) % NUM_MUSIC_TRACKS;

    if (musictrack1_playing == M_NONE)
    {
        musictrack1_playing = M_END_SOMETHING;
    }

    musicTrack1Play(musictrack1_playing);
}

/**
 * Unreferenced.
 */
void lvlMusicPlayStageTrackOrRandom(void)
{
    lvlPlayMusicTrack1(getmusictrack_or_randomtrack(g_CurrentStageToLoad));
}

/**
 * Stage load method.
 * Title screen is handled as a special case.
 * First half of method resets stage and player values (including mutliplayer values) to defaults.
 * Second part loads stage data (init guards, init guard heads, etc).
 *
 * NTSC Address: 0x7F0BDAB0.
 * NTSC-J Address: 7F0BE660.
 * PAL Address: 7F0BCE60.
 **/
void lvlStageLoad(s32 stage)
{
    s32 i;
    struct player_data *player_data;

    g_CurrentStageToLoad = stage;

    // this if block pushes where g_CurrentStageToLoad gets loaded to the
    // top of the method. Maybe a debug log about which level is loaded.
    if(0)
    {
        #ifdef DEBUG
        // removed
        #endif
    }

    D_800483C0 = 1;
    g_ControlsLockedFlag = 0;
    g_ClockTimer = 1;

#ifdef VERSION_US
    g_GlobalTimerDelta = 1.0f;
#endif
#if defined(VERSION_JP) || defined(VERSION_EU)
    g_JP_GlobalTimerDelta = 1.0f;
#endif

    D_80048380 = 0;
    g_GlobalTimer = 0;

#if defined(VERSION_JP)
    g_GlobalTimerDelta = 1.f;
#endif
#if defined(VERSION_EU)
    g_GlobalTimerDelta = 1.20000004768f;
#endif

    D_80048388 = 0;
    D_8004838C = 0;
    D_80048390 = 0;
    D_80048394 = 0;
    g_CurrentMultiPlayerSec = 0.0f;
    D_800483B0 = 0;
    g_StageTimeSec = 0.0f;
    g_MpSoundStateRelated = 0;

    sndSetScalerApplyVolumeAllSfxSlot(1.0f);
    musicTrack1ApplySeqpVol(VOLUME_MAX);
    musicTrack2ApplySeqpVol(VOLUME_MAX);
    musicTrack3ApplySeqpVol(VOLUME_MAX);
    sub_GAME_7F0C1364();
    modelmgrSetLevelResetting(TRUE);
    set_mt_tex_alloc();
#ifdef VERSION_EU
    bullet_moving_sparks_reset();
#else
    bullet_sparks_reset_all();
#endif
    texReset();
    load_font_tables();

    /* If title screen, initialize screen and folder setup.
    * Otherwise:
    * - enable cheats for player
    * - init watch
    * - reset some player values, like view height
    * - reset multiplayer stats
    */
    if (stage == LEVELID_TITLE)
    {
        init_menus_or_reset();
    }
    else
    {
        g_NewCheatUnlocked = 0;

        if ((g_CurrentStageToLoad != LEVELID_TITLE) && (D_80048394 == 0) && (g_ClockTimer > 0))
        {
            if (g_AppendCheatSinglePlayer != 0)
            {
                s32 s0 = 1;

                for (s0 = 1; s0 != CHEAT_INVALID; s0++)
                {
                    if (g_CheatActivated[s0] && cheatIsEnemyRockets(s0))
                    {
                        cheatButtonTurnOnCheatForPlayers(s0);
                    }
                }
            }
        }

        load_bg_file(g_CurrentStageToLoad);
        skySetStageNum(g_CurrentStageToLoad);

        // HACK: This method call is wrong. The function takes one argument, but the asm calls it without
        // any arguments here.
        init_watch_at_start_of_stage();

        sub_GAME_7F0C11FC(stage);

        for (i=0; i<4; i++)
        {
            s32 s3;
            player_data = (struct player_data *)&g_playerPlayerData[i];

            if (getPlayerCount() == 1)
            {
                // s4 variable
                player_data->autoaim = 0;
                player_data->sight = 0;
                player_data->handicap = 1.0f;
                player_data->player_perspective_height = 1.0f;
            }
            else
            {

                // why is this looping from g_playerPlayerData again, this inner block
                // gets executed 16 times in multiplayer.
                for (s3 = 0; s3 < 4; s3++)
                {
                    if (get_scenario() == SCENARIO_LTK)
                    {
                        g_playerPlayerData[s3].handicap = 200.0f;
                    }
                    else
                    {
                        g_playerPlayerData[s3].handicap = get_player_mp_handicap(s3);
                    }

                    g_playerPlayerData[s3].player_perspective_height = get_player_mp_char_height(s3);
                }

                lvlSetMpTime(get_mp_timelimit());
                lvlSetMpPoint(get_mp_pointlimit());
                copy_aim_settings_to_playerdata();
            }

            // g_playerPlayerData s4 variable
            player_data->time_other_players_on_screen = 0;
            player_data->damage_to_backside = 0;
            player_data->min_time_between_kills = S32_MAX;
            player_data->max_time_between_kills = 0;
            player_data->most_killed_one_life = 0;
            player_data->most_killed_one_time = 0;
            player_data->longest_inning = 0;
            player_data->shortest_inning = S32_MAX;
            player_data->order_out_in_yolt = 0;
            player_data->flag_counter = 0;
            player_data->distance_traveled = 0.0f; // one kind of float zero
            player_data->body_armor_pickups = 0.f; // a different kind of float zero

            // g_playerPlayerData s2, different than above
            for (s3 = 0; s3 < 4; s3++)
            {
                player_data->kill_counts[s3] = 0;
            }
        }
    }

    something_with_stage_objectives();
    mpwatchUnpauseGame();
    sub_GAME_7F09B820();
    initModelHitEntryFreeList();
    modelmgrResetSlotCounts();
    init_load_objpos_table();
    reinit_between_menus();
    init_sound_effects_registers();
    init_guards();
    bodiesReset(stage);
    proplvreset2(stage);
    alloc_explosion_smoke_casing_scorch_impact_buffers();
    alloc_shattered_window_pieces();
    sub_GAME_7F007290();
    initCheatTextBuffer();

    if (g_CurrentStageToLoad == LEVELID_TITLE)
    {
        disableOnscreenCheatText();
    }
    else
    {
        s32 s0;

        init_path_table_links();
        init_ejected_cartridges();

        for (s0 = 0; s0 < getPlayerCount(); s0++)
        {
            set_cur_player(s0);
            reinit_gunheld_totaltime();
            init_player_BONDdata_stats();
            init_player_BONDdata();
            bondviewLoadSetupIntroSection();
            bondviewPlayerBeginLife();
            sets_a_bunch_of_BONDdata_values_to_default();
            disableOnscreenCheatText();
        }

        set_cur_player(0);
    }

    /**
     * Leave stage load allocation mode.
     * From this point on, model creation should try to reuse existing slots.
     */
    modelmgrSetLevelResetting(FALSE);
    
    zbufDeallocate();
    viSetVideoMode(MD_NORMAL);
    D_80048368 = 1.0f;
    lvlSetControlsLockedFlag(0);
}



s32 lvlGetCurrentStageToLoad(void)
{
    return g_CurrentStageToLoad;
}


 /**
 * Address: 7F0BDF10
 * 
 * Debug method. Something to do with portals. Button press
 * on controller 1 and 2 are used for control flow.
 */
Gfx *lvlPortalDebug7F0BDF10(Gfx *gdl)
{
    s32 temp_v1;
    bool portalDebugStateChanged; // This value has no observable use in the final assembly but the variable definitely existed in the orignal code.
    s32 *selectedPortal = &g_DebugPortalIndex;

    portalDebugStateChanged = FALSE;

    if (gdl)
    {
        gdl = bgDebugRemoved7F0B9DE4(gdl, g_DebugPortalIndex, -1);

        g_DebugPortalsInputBuffer = g_DebugPortalsInputBufferSource1;
        g_DebugPortalsInputBuffer1 = g_DebugPortalsInputBufferSource2;
        temp_v1 = g_DebugPortalsInputBufferSource4;
        g_DebugPortalsInputBuffer2 = g_DebugPortalsInputBufferSource3;
        g_DebugPortalsInputBuffer3 = temp_v1;

        temp_v1 = joyGetButtons(PLAYER_1, A_BUTTON) | joyGetButtons(PLAYER_2, A_BUTTON);

        if ((temp_v1 == g_DebugPortalsInputBuffer3) == FALSE)
        {
            D_800483C0 ^= 1;
        }

        if ((g_DebugPortalsInputBuffer1 == g_DebugPortalsInputBuffer) == FALSE)
        {
            D_800483C0 ^= 1;
        }

        g_DebugPortalsInputBuffer4 = temp_v1;

        bgRemoved7F0B9DF4(temp_v1 ? g_DebugPortalIndex : -1);

        return gdl;
    }

    if (joyGetButtonsPressedThisFrame(PLAYER_1, L_JPAD) |
        joyGetButtonsPressedThisFrame(PLAYER_2, L_JPAD))
    {
        g_DebugPortalIndex = *selectedPortal - 1;

        if (portalDebugStateChanged = TRUE, *selectedPortal < 0)
        {
            g_DebugPortalIndex = 0;
        }
    }

    if (joyGetButtonsPressedThisFrame(PLAYER_1, R_JPAD) |
        joyGetButtonsPressedThisFrame(PLAYER_2, R_JPAD))
    {
        g_DebugPortalIndex = *selectedPortal + 1;
        portalDebugStateChanged = TRUE;
    }

    if ((joyGetButtons(PLAYER_1, R_TRIG) | joyGetButtons(PLAYER_2, R_TRIG)) &&
        (joyGetButtons(PLAYER_1, L_TRIG) | joyGetButtons(PLAYER_2, L_TRIG)))
    {
        if (joyGetButtonsPressedThisFrame(PLAYER_1, D_JPAD))
        {
            bgSwapConnectedRooms(*selectedPortal);
        }
    }
    else if (joyGetButtons(PLAYER_1, R_TRIG) | joyGetButtons(PLAYER_2, R_TRIG))
    {
        if ((joyGetButtonsPressedThisFrame(PLAYER_1, D_JPAD) |
             joyGetButtonsPressedThisFrame(PLAYER_2, D_JPAD)) &&
            (bgGetDataPortalsControlBytes1Bit1(g_DebugPortalIndex) == 0))
        {
            bgToggleDataPortalsContrlBytes1Bit1(g_DebugPortalIndex, 0);
            portalDebugStateChanged = TRUE;
        }

        if ((joyGetButtonsPressedThisFrame(PLAYER_1, U_JPAD) |
             joyGetButtonsPressedThisFrame(PLAYER_2, U_JPAD)) &&
            (bgGetDataPortalsControlBytes1Bit1(g_DebugPortalIndex) != 0))
        {
            bgToggleDataPortalsContrlBytes1Bit1(g_DebugPortalIndex, 1);
            portalDebugStateChanged = TRUE;
        }
    }
    else if (joyGetButtons(PLAYER_1, L_TRIG) | joyGetButtons(PLAYER_2, L_TRIG))
    {
        if ((joyGetButtonsPressedThisFrame(PLAYER_1, D_JPAD) |
             joyGetButtonsPressedThisFrame(PLAYER_2, D_JPAD)) &&
            bgGetDataPortalsControlBytes1Bit2(g_DebugPortalIndex))
        {
            bgClearDataPortalsControlBytes1Low2Bits(g_DebugPortalIndex);
            portalDebugStateChanged = TRUE;
        }

        if ((joyGetButtonsPressedThisFrame(PLAYER_1, U_JPAD) |
             joyGetButtonsPressedThisFrame(PLAYER_2, U_JPAD)) &&
            (bgGetDataPortalsControlBytes1Bit2(g_DebugPortalIndex) == 0))
        {
            bgSetDataPortalsControlBytes1Bit2(g_DebugPortalIndex);
            portalDebugStateChanged = TRUE;
        }
    }
    else
    {
        if (joyGetButtonsPressedThisFrame(PLAYER_1, D_JPAD) |
            joyGetButtonsPressedThisFrame(PLAYER_2, D_JPAD))
        {
            sub_GAME_7F0B9A7C(g_DebugPortalIndex);
            portalDebugStateChanged = TRUE;
        }

        if (joyGetButtonsPressedThisFrame(PLAYER_1, U_JPAD) |
            joyGetButtonsPressedThisFrame(PLAYER_2, U_JPAD))
        {
            sub_GAME_7F0B9A2C(g_DebugPortalIndex);
            portalDebugStateChanged = TRUE;
        }
    }

    if (portalDebugStateChanged)
    {
        // Removed
    }

    return NULL;
}


/**
 * Graphics render method.
 * Also sets player max ammo if infinite ammo cheat is enabled.
 *
 * Address 0x7F0BE30C (VERSION_US).
 */

Gfx* lvlRender(Gfx* DL)
{
    gSPSegment(DL++, SPSEGMENT_PHYSICAL, NULL);
    gSPSegment(DL++, SPSEGMENT_UNKNOWN, osVirtualToPhysical(ptr_font_DL));

    gSPDisplayList(DL++, &dlFastPipelineSetup);
    gSPDisplayList(DL++, &dlZBufferGeometry);

    if (g_CurrentStageToLoad == LEVELID_TITLE)
    {
        DL = viClearZBufCurrentPlayer(DL);
        DL = viSetupCurrentPlayerView(DL);
        gDPSetScissor(DL++, G_SC_NON_INTERLACE, 0, 0, (s16)viGetX(), (s16)viGetY());
        DL = menu_jump_constructor_handler(DL);
    }
    else
    {
        s32 i;
        s32 pcount;

        pcount = getPlayerCount();

        gSPClipRatio(DL++, FRUSTRATIO_2);

        for(i = 0; i < pcount; i++)
        {
            set_cur_player(get_nth_player_from_shuffled(i));

            viSetViewSize(g_CurrentPlayer->viewx, g_CurrentPlayer->viewy);
            viSetViewPosition(g_CurrentPlayer->viewleft, g_CurrentPlayer->viewtop);
            viSetFovY(g_CurrentPlayer->fovy);
            viSetAspect(g_CurrentPlayer->aspect);

            DL = viClearZBufCurrentPlayer(DL);
            DL = viSetupCurrentPlayerView(DL);

            if (get_debug_render_raster() == DEB_MOVE_VIEW)
            {
                DL = sub_GAME_7F091580(DL);
            }

            if (get_debug_render_raster() == DEB_STAN_VIEW)
            {
                DL = stanRenderDebugStanView(DL);
            }

            if (get_debug_render_raster() == DEB_BOND_VIEW)
            {
                DL = bondviewRenderDebugBondView(DL);
            }

            DL = viSetupScreensForNumPlayers(DL);
            DL = skyRender(DL);
            bgRoomVisibilityRelated();
            propsTick();
            chraiUpdateOnscreenPropCount();
            chrpropUpdateAutoaimTarget();
            chraiCheckUseHeldItems();
#ifdef SIGHTLINE_ROM_DEBUG
            /* The debug oracle samples HERE, not earlier: propsTick() above
             * has already written this frame's calculatedopacity and set
             * PROPFLAG_ONSCREEN, and the view matrices are in exactly the
             * state the shot chraiCheckUseHeldItems() may just have fired
             * saw. Reading anywhere else answers a different frame. */
            if (i == 0) { slRomDbgTick(); }
#endif

            if (bond_pressed_reload_activate() && bond_interact_object())
            {
                attempt_reload_item_in_hand(GUNRIGHT);
                attempt_reload_item_in_hand(GUNLEFT);
            }
#ifndef __sgi
            /* NATIVE INTERACT and RELOAD, the two halves of the contextual
             * line above, each on its own. Both flags are written in
             * bondviewProcessInput (src/game/bondview2.c, "NATIVE ACTIONS")
             * and neither can be set by a recorded stream, a headless run
             * or a gamepad, so this block is inert everywhere the line
             * above is the whole story. Declared here, inside this file's
             * own #ifndef __sgi, so IDO sees zero tokens.
             *
             * INTERACT: the same call, the same frame position, the return
             * ignored - so E in front of nothing does nothing, and E never
             * reloads. RELOAD: the same two calls the line above makes on a
             * TRUE return, without asking bond_interact_object at all - so R
             * in front of a door reloads and leaves the door alone. */
            {
                extern s32 sl_bond_pressed_interact(void);
                extern s32 sl_bond_pressed_reload(void);

                if (sl_bond_pressed_interact())
                {
                    bond_interact_object();
                }

                if (sl_bond_pressed_reload())
                {
                    attempt_reload_item_in_hand(GUNRIGHT);
                    attempt_reload_item_in_hand(GUNLEFT);
                }
            }
#endif

            propsTickPlayer();
            DL = bgLevelRender(DL);

            if (get_debug_portal_flag())
            {
                DL = lvlPortalDebug7F0BDF10(DL);
            }

            if (get_debug_stan_problems_flag())
            {
                DL = sub_GAME_7F0B303C(DL);
            }

            if (get_debug_stanhit_flag())
            {
                DL = sub_GAME_7F0B3034(DL);
                DL = write_stan_tiles_in_yellow(DL);
            }

            if (get_debug_stanregion_flag())
            {
                DL = sub_GAME_7F0B3034(DL);
                DL = sub_GAME_7F0B312C(DL, -0x7FC0);
            }

            if (tokenFind(1, "-stanshow_"))
            {
                StandTilePoint *tile1 = stanMatchTileName(tokenFind(1, "-stanshow_"));
                if (tile1)
                {
                    DL = sub_GAME_7F0B3034(DL);
                    DL = sub_GAME_7F0B3024(DL, tile1, 0xFF0000FF);
                }
            }

            if (tokenFind(2, "-stanshow_"))
            {
                StandTilePoint *tile2 = stanMatchTileName(tokenFind(2, "-stanshow_"));
                if (tile2)
                {
                    DL = sub_GAME_7F0B3034(DL);
                    DL = sub_GAME_7F0B3024(DL, tile2, 0xFF00FF);
                }
            }

            if (tokenFind(3, "-stanshow_"))
            {
                StandTilePoint *tile3 = stanMatchTileName(tokenFind(3, "-stanshow_"));
                if (tile3)
                {
                    DL = sub_GAME_7F0B3034(DL);
                    DL = sub_GAME_7F0B3024(DL, tile3, 0xFFFF);
                }
            }

            setanimationdebugflag(getDebugMode() == DEB_SELANIM);
            DL = weaponRenderTracers(DL);

#if defined(VERSION_EU)
            bullet_moving_sparks_update(&DL, ZBUF_SURFACE);
#else /* VERSION_US, VERSION_JP, unspecified */
            bullet_sparks_render_all(&DL, ZBUF_SURFACE);
#endif
            DL = glassRenderShards(DL);
            DL = explosionRenderFlyingParticles(DL);

            if (

#if defined(BUGFIX_R1)
                cheatIsActive(CHEAT_INFINITE_AMMO) != 0
                && (
                    (getCurrentPlayerWeaponId(GUNRIGHT) != ITEM_WATCHLASER)
                    || (g_CurrentPlayer->trigger_down == 0)
                )
#else /* VERSION_US */
                cheatIsActive(CHEAT_INFINITE_AMMO) != 0
#endif
                )
            {
                set_max_ammo_for_cur_player();
            }

            if (get_debug_render_raster() == DEB_BOND_VIEW)
            {
                DL = maybe_mp_interface(DL);
            }
            else
            {
                DL = bondviewRemoved7F08BCB8(DL);
            }

            DL = mp_watch_menu_display(DL);
        }
    }

    gDPSetScissor(DL++, G_SC_NON_INTERLACE, 0, 0, viGetX(), viGetY());

    return DL;
}


/**
 * Sets the modifier values for the level being loaded.
 * This covers the enemy accuracy, reaction speed, and similar values.
 *
 * address 0x7F0BE8D0
 */
void lvlSetMultipliersForDifficulty(void)
{
    if (g_SelectedDifficulty == DIFFICULTY_AGENT)
    {
        f32 armorDiff = currentPlayerGetHealth() + currentPlayerGetArmor();
        f32 damageMultiplier = 1.0f;

        if (armorDiff <= 0.125f)
        {
            damageMultiplier = 0.5f;
        }
        else if (armorDiff <= 0.6f)
        {
            damageMultiplier = (((armorDiff - 0.125f) * 0.5f) / 0.47500002f) + 0.5f;
        }

        F_80030B14 = 2.0f;
        F_80030B18 = 2.0f;
        g_AutogunPendingDamageTick = (0.5f * damageMultiplier);
        g_AutogunDamageScalar = (0.5f * damageMultiplier);
        F_80030B24 = 2.0f;
        g_AiAccuracyModifier = DEFAULT_AGENT_AI_ACCURACY_MODIFIER;
        g_AiDamageModifier = (DEFAULT_AGENT_AI_DAMAGE_MODIFIER * damageMultiplier);
        g_AiHealthModifier = 2.0f;
        g_SpExplosionDamageMult = (f32) (0.25f * damageMultiplier);
        difficulty = 1.5f;
        g_SoloAmmoMultiplier = DEFAULT_AGENT_SOLO_AMMO_MULTIPLIER;
        g_AiReactionSpeed = DEFAULT_AGENT_AI_REACTION_SPEED;
    }
    else if (g_SelectedDifficulty == DIFFICULTY_SECRET)
    {
        F_80030B14 = 1.0f;
        F_80030B18 = 1.0f;
        g_AutogunPendingDamageTick = 0.75f;
        g_AutogunDamageScalar = 0.75f;
        F_80030B24 = 1.0f;
        g_AiAccuracyModifier = DEFAULT_SECRET_AGENT_AI_ACCURACY_MODIFIER;
        g_AiDamageModifier = DEFAULT_SECRET_AGENT_AI_DAMAGE_MODIFIER;
        g_AiHealthModifier = 1.0f;
        g_SpExplosionDamageMult = 0.75f;

#if defined(BUGFIX_R1)
        if (j_text_trigger)
        {
            difficulty = 1.1f;
        }
        else
        {
            difficulty = 0.75f;
        }
#else
        // VERSION_US
        difficulty = 0.75f;
#endif

        g_SoloAmmoMultiplier = DEFAULT_SECRET_AGENT_SOLO_AMMO_MULTIPLIER;
        g_AiReactionSpeed = DEFAULT_SECRET_AGENT_AI_REACTION_SPEED;
    }
    else if (g_SelectedDifficulty == DIFFICULTY_00)
    {
        F_80030B14 = 1.0f;
        F_80030B18 = 1.0f;
        g_AutogunPendingDamageTick = 1.0f;
        g_AutogunDamageScalar = 1.0f;
        F_80030B24 = 1.0f;
        g_AiAccuracyModifier = DEFAULT_00_AGENT_AI_ACCURACY_MODIFIER;
        g_AiDamageModifier = DEFAULT_00_AGENT_AI_DAMAGE_MODIFIER;
        g_AiHealthModifier = 1.0f;
        g_SpExplosionDamageMult = 1.0f;

#if defined(BUGFIX_R1)
        if (j_text_trigger)
        {
            difficulty = 0.75f;
        }
        else
        {
            difficulty = 0.2f;
        }
#else
        // VERSION_US
        difficulty = 0.2f;
#endif

        g_SoloAmmoMultiplier = DEFAULT_00_AGENT_SOLO_AMMO_MULTIPLIER;
        g_AiReactionSpeed = DEFAULT_00_AGENT_AI_REACTION_SPEED;
    }
    else if (g_SelectedDifficulty == DIFFICULTY_007)
    {
        F_80030B14 = 1.0f;
        F_80030B18 = 1.0f;
        g_AutogunPendingDamageTick = 1.0f;
        g_AutogunDamageScalar = 1.0f;
        F_80030B24 = 1.0f;
        g_AiAccuracyModifier = DEFAULT_007_AI_ACCURACY_MODIFIER;
        g_AiDamageModifier = DEFAULT_007_AI_DAMAGE_MODIFIER;
        g_AiHealthModifier = 1.0f;
        g_SpExplosionDamageMult = 1.0f;
        difficulty = 1.0f;
        g_SoloAmmoMultiplier = DEFAULT_007_SOLO_AMMO_MULTIPLIER;
        g_AiReactionSpeed = DEFAULT_007_AI_REACTION_SPEED;
    }
}


/**
 * Multiplayer method. Manages a lot of stuff.
 * Tracks you-only-live-twice kills/deaths.
 * Lots of debug code.
 *
 * Address: 0x7F0BEB88 (NTSC).
 * Address: 0x7F0BF7AC (NTSC-J).
 * Address: 0x7F0BDFAC (PAL).
 */
void lvlManageMpGame(void)
{
    tlbmanageResetCurrentEntriesCount();

    if (g_ControlsLockedFlag != 0)
    {
        g_ClockTimer = 0;
    }
    else if (checkGamePaused() != 0)
    {
        g_ClockTimer = 0;
    }
    else
    {
        g_ClockTimer = speedgraphframes;
        D_80048380 += 1;
    }

#ifdef VERSION_US
    g_GlobalTimerDelta = (f32) g_ClockTimer;
#else
    g_JP_GlobalTimerDelta = (f32) g_ClockTimer;
#ifdef VERSION_EU
    g_GlobalTimerDelta = g_JP_GlobalTimerDelta * 1.2f;
#else
    g_GlobalTimerDelta = g_JP_GlobalTimerDelta;
#endif
#endif
    g_GlobalTimer += g_ClockTimer;
    if ((g_CurrentStageToLoad != LEVELID_TITLE) && (D_80048394 == 0) && (g_ClockTimer > 0))
    {
        if (g_AppendCheatSinglePlayer != 0)
        {
            s32 i;
            for (i = 1; i != CHEAT_INVALID; i++)
            {
                if (g_CheatActivated[i] && !cheatIsEnemyRockets(i))
                {
                    cheatButtonTurnOnCheatForPlayers(i);
                }
            }
        }
    }

    if ((getPlayerCount() >= 2) && (g_CurrentStageToLoad != LEVELID_TITLE))
    {
        if (get_mission_state() == MISSION_STATE_6)
        {
            s32 i;
            s32 mp_alive_count;
            s32 mp_player_field424_count;

            mp_alive_count = 0;
            mp_player_field424_count = 0;

            for (i=0; i<getPlayerCount(); i++)
            {
                if (g_playerPointers[i]->bonddead != FALSE)
                {
                    mp_alive_count++;
                    if (g_playerPointers[i]->redbloodfinished)
                    {
                        mp_player_field424_count++;
                    }
                }
            }

            if ((mp_alive_count > 0) && (mp_alive_count == mp_player_field424_count))
            {
                set_missionstate(MISSION_STATE_1);
            }
        }

        if (g_MpTime > 0)
        {
            s32 current_time;
            s32 sp180;
            s32 i;
            current_time = D_80048394;
            sp180 = g_ClockTimer + D_80048394;

#ifdef VERSION_EU
            if ((D_80048394 < (g_MpTime - 0xBB8)) && (sp180 >= (g_MpTime - 0xBB8)))
#else
            if ((D_80048394 < (g_MpTime - 0xE10)) && (sp180 >= (g_MpTime - 0xE10)))
#endif
            {
                for (i = 0; i < getPlayerCount(); i++)
                {
                    set_cur_player(i);
#ifdef VERSION_US
                    HUDMESSAGEBOTTOM("One minute left");
#else
                    HUDMESSAGEBOTTOM(langGet(0xB044));
#endif
                }
            }

            // sound alarm when game is about to end (10 seconds before end)
#ifdef VERSION_EU
            if ((sp180 >= (g_MpTime - 0x1F4)) && (g_MpSoundStateRelated == 0) && (lvlGetControlsLockedFlag() == 0))
#else
            if ((sp180 >= (g_MpTime - 0x258)) && (g_MpSoundStateRelated == 0) && (lvlGetControlsLockedFlag() == 0))
#endif
            {
                sndPlaySfx(g_musicSfxBufferPtr, ALARM1_SFX, &g_MpSoundStateRelated);
            }

            // stop alarm
            if (lvlGetControlsLockedFlag() != 0)
            {
                if ((g_MpSoundStateRelated != NULL) && (sndGetPlayingState(g_MpSoundStateRelated) != 0))
                {
                    sndDeactivate(g_MpSoundStateRelated);
                }
            }

            if ((current_time < g_MpTime) && (sp180 >= g_MpTime))
            {
                mpCalculateAwards(FALSE);
            }
        }

        // when playing with a kill limit, g_MpPoint is not zero
        if ((g_MpPoint > 0) && (g_ClockTimer != 0))
        {
            s32 var_player_count1;
            s32 i;
            s32 mp_player_currently_in_dying_animation;
            s32 mp_players_over_point_limit;

            var_player_count1 = getPlayerCount();
            mp_player_currently_in_dying_animation = 0;
            mp_players_over_point_limit = 0;

            for (i = 0; i < var_player_count1; i++)
            {
                if (g_playerPointers[i]->bonddead != FALSE &&
                    (g_playerPointers[i]->redbloodfinished == FALSE || g_playerPointers[i]->deathanimfinished == FALSE || g_playerPointers[i]->colourfadetimemax60 >= 0.0f))
                {
                    mp_player_currently_in_dying_animation++;
                }

                if (get_points_for_mp_player(i) >= g_MpPoint)
                {
                    // counts players over kill limit
                    mp_players_over_point_limit++;
                }
            }

            if (mp_players_over_point_limit > 0)
            {
                if (mp_player_currently_in_dying_animation == 0)
                {
                    // end game after dying players are finished dying
                    mpCalculateAwards(FALSE);
                }
                else
                {
                    // this will cause the game to freeze players, to stop them from moving once game ended
                    mpwatchSetStopPlayFlag();
                }
            }
        }


        // YOLT scenario: end-of-game tracking.
        if ((get_scenario() == SCENARIO_YOLT) && (g_ClockTimer != 0))
        {
            s32 player_count;
            s32 killed_count;
            s32 not_dead_count;
            s32 fully_dead_total;
            s32 killed_total;
            s32 i;
            s32 j;

            player_count = getPlayerCount();
            killed_total = 0;
            fully_dead_total = 0;

            for (i = 0; i < player_count; i++)
            {
                killed_count = 0;
                not_dead_count = 0;

                for (j = 0; j < player_count; j++)
                {
                    if (g_playerPointers[j]->bonddead == 0)
                    {
                        not_dead_count++;
                    }
                    killed_count += g_playerPlayerData[j].kill_counts[i];
                }

                if (killed_count >= 2)
                {
                    if (g_playerPlayerData[i].order_out_in_yolt == 0)
                    {
                        g_playerPlayerData[i].order_out_in_yolt = (u8) (not_dead_count + 1);
                    }

                    killed_total++;

                    if (g_playerPointers[i]->redbloodfinished
                        && g_playerPointers[i]->deathanimfinished
                        && g_playerPointers[i]->colourfadetimemax60 < 0.0f)
                    {
                        fully_dead_total++;
                    }
                }
            }

            if (fully_dead_total >= player_count - 1)
            {
                mpCalculateAwards(FALSE);
            }
            else if (killed_total >= player_count - 1)
            {
                mpwatchSetStopPlayFlag();
            }
        } // end YOLT

        if (0)
        {
            char debug_buf[268];
            sprintf(debug_buf, "setdetail %d %d %d %d %d %d %d %d %d", 0, 0, 0, 0, 0, 0, 0, 0, 0);
        }
    }

    D_80048394 = D_80048394 + g_ClockTimer;
#ifdef VERSION_EU
    g_CurrentMultiPlayerSec = (f32) (D_80048394) / 50.0f;
#else
    g_CurrentMultiPlayerSec = (f32) (D_80048394) / 60.0f;
#endif
    D_800483A8 = D_800483A8 + g_ClockTimer;
#ifdef VERSION_EU
    g_CurrentMultiPlayerMin = (f32) (D_800483A8) / 50.0f;
#else
    g_CurrentMultiPlayerMin = (f32) (D_800483A8) / 60.0f;
#endif

    if (joyGetButtonsPressedThisFrame(PLAYER_1, ANY_BUTTON))
    {
        D_80048388 = 0;
        D_80048390 = 0;
    }
    else
    {
        D_80048390 = D_80048390 + g_ClockTimer;

#ifdef VERSION_EU
        if (D_80048390 >= 0x5DC)
#else
        if (D_80048390 >= 0x708)
#endif
        {
            D_80048388 = 1;
        }
    }

    if (D_80048388 != 0)
    {
        D_8004838C += g_ClockTimer;
    }
    else
    {
        D_800483B0 = D_800483B0 + g_ClockTimer;
#ifdef VERSION_EU
        g_StageTimeSec = (f32) (D_800483B0) / 50.0f;
#else
        g_StageTimeSec = (f32) (D_800483B0) / 60.0f;
#endif
        D_800483B8 = D_800483B8 + g_ClockTimer;
#ifdef VERSION_EU
        g_PowerOnTimeSec = (f32) (D_800483B8) / 50.0f;
#else
        g_PowerOnTimeSec = (f32) (D_800483B8) / 60.0f;
#endif
    }

    viSetUseZBuf(1);

    if (g_CurrentStageToLoad == LEVELID_TITLE)
    {
        cheat_buttons_mp_related();
        menu_init();
        langTick();
    }
    else
    {
        sub_GAME_7F09BBBC();
        lvlSetMultipliersForDifficulty();
        updateRoomStatusFlags();
        sub_GAME_7F092E50();
        skyTick();
#ifdef VERSION_EU
        bullet_moving_spark_create();
#else
        bullet_sparks_update_all();
#endif
        update_bullet_casings();
        update_broken_windows();
        explosionUpdateFlyingParticles();
        chrpropTick();
        reset_all_music_slots();
        langTick();

        if ((get_debug_joy2detailedit_flag() != 0) && (D_800483C8 == 0))
        {
            s32 i;
            D_800483C8 = (struct LvlMpUnknown *)mempAllocBytesInBank(0x3000, MEMPOOL_STAGE);
            if (D_800483C8 != 0)
            {
                for (i=0; i<3000; i++)
                {
                    D_800483C8[i].unk_0 = 0xff;
                    D_800483C8[i].unk_1 = (D_800483C8[i].unk_1 & 0xFF1F) | 0x20;
                    D_800483C8[i].unk_1 = (D_800483C8[i].unk_1 & 0xFFE3) | 4;
                }
            }
        }

        if (get_debug_portal_flag() != 0)
        {
            lvlPortalDebug7F0BDF10(0);
        }

        switch (getDebugMode())
        {
            case 4:
            {
                if (joyGetButtonsPressedThisFrame(PLAYER_1, L_CBUTTONS))
                {
                    sub_GAME_7F0AF630(-1);
                    debugStanView(0, 0, 0);
                }

                if (joyGetButtonsPressedThisFrame(PLAYER_1, R_CBUTTONS))
                {
                    sub_GAME_7F0AF630(1);
                    debugStanView(0, 0, 0);
                }
            }
            break;

            case 8:
            {
                if (joyGetButtonsPressedThisFrame(PLAYER_1, L_CBUTTONS))
                {
                    chrDecrementAnimationTablePointerCount();
                }

                if (joyGetButtonsPressedThisFrame(PLAYER_1, R_CBUTTONS))
                {
                    chrIncrementAnimationTablePointerCount();
                }

                if (joyGetButtonsPressedThisFrame(PLAYER_1, L_TRIG))
                {
                    chrToggleD_8002C90C();
                }

                sub_GAME_7F022EE0(joyGetButtons(PLAYER_1, R_TRIG) != 0);
            }
            break;
        }

    }
    {
        struct ALBank * sfx;
        s16 sound_index;
        s16 *sound_index_ptr;
        switch (getDebugMode())
        {
            case 0x38:
            {
                s32 sp30;
                s32 sp2C;
                sp30 = viGetHorizontalOffset();
                sp2C = viGet800232A0();
                if (joyGetButtons(PLAYER_1, D_CBUTTONS))
                {
                    sp2C += 1;
                }
                if (joyGetButtons(PLAYER_1, U_CBUTTONS))
                {
                    sp2C += -1;
                }
                if (joyGetButtons(PLAYER_1, R_CBUTTONS))
                {
                    sp30 += 1;
                }
                if (joyGetButtons(PLAYER_1, L_CBUTTONS))
                {
                    sp30 += -1;
                }
                viSetHorizontalOffset(sp30);
                viSet800232A0(sp2C);
            }
            break;

            case 0xc:
            {
                if (joyGetButtonsPressedThisFrame(PLAYER_1, (L_JPAD | L_CBUTTONS)))
                {
                    lvlMusicAppendPlayEndTheme();
                }

                if (joyGetButtonsPressedThisFrame(PLAYER_1, (R_JPAD | R_CBUTTONS)))
                {
                    lvlMusicAppendPlaySoloDeathShort();
                }

                if (joyGetButtonsPressedThisFrame(PLAYER_1, D_JPAD))
                {
                    musicTrack1Stop();
                }

                if (joyGetButtonsPressedThisFrame(PLAYER_1, B_BUTTON))
                {
                    musicTrack1SaveCurrentVolumeAsTrackDefault();
                }
            }
            break;
            case 0xd:
            {
#ifdef VERSION_US
                sound_index_ptr = &g_DebugMpGameSoundFxIndex;

                if (joyGetButtonsPressedThisFrame(PLAYER_1, (D_JPAD | L_JPAD | L_TRIG | L_CBUTTONS)))
                {
                    sound_index = g_DebugMpGameSoundFxIndex - 1;\
                    sfx = g_musicSfxBufferPtr;\
                    *sound_index_ptr = sound_index;
                    sndPlaySfx(sfx, sound_index, NULL);
                }

                if (joyGetButtonsPressedThisFrame(PLAYER_1, (U_JPAD | R_JPAD | R_TRIG | R_CBUTTONS)))
                {
                    sound_index = g_DebugMpGameSoundFxIndex + 1;\
                    sfx = g_musicSfxBufferPtr;\
                    *sound_index_ptr = sound_index;
                    sndPlaySfx(sfx, sound_index, NULL);
                }

                if (joyGetButtonsPressedThisFrame(PLAYER_1, D_CBUTTONS))
                {
                    sndDeactivateAllSfxByFlag_1();
                }

                if (joyGetButtonsPressedThisFrame(PLAYER_1, U_CBUTTONS))
                {
                    sound_index = g_DebugMpGameSoundFxIndex;\
                    sfx = g_musicSfxBufferPtr;
                    sndPlaySfx(sfx, sound_index, NULL);
                }
#else
                sound_index_ptr = &g_DebugMpGameSoundFxIndex;

                if (joyGetButtonsPressedThisFrame(PLAYER_1, (D_JPAD | L_JPAD | L_TRIG | L_CBUTTONS)))
                {
                    sound_index = g_DebugMpGameSoundFxIndex - 1;\
                    sfx = g_musicSfxBufferPtr;\
                    *sound_index_ptr = sound_index;
                    sndPlaySfx(sfx, sound_index, NULL);
                }

                if (joyGetButtonsPressedThisFrame(PLAYER_1, (U_JPAD | R_JPAD | R_TRIG | R_CBUTTONS)))
                {
                    sound_index = g_DebugMpGameSoundFxIndex + 1;\
                    sfx = g_musicSfxBufferPtr;\
                    *sound_index_ptr = sound_index;
                    sndPlaySfx(sfx, sound_index, NULL);
                }

                if (joyGetButtonsPressedThisFrame(PLAYER_1, D_CBUTTONS))
                {
                    sndDeactivateAllSfxByFlag_1();
                }

                if (joyGetButtonsPressedThisFrame(PLAYER_1, U_CBUTTONS))
                {
                    sound_index = g_DebugMpGameSoundFxIndex;\
                    sfx = g_musicSfxBufferPtr;
                    sndPlaySfx(sfx, sound_index, NULL);
                }
#endif
            }
            break;

            default:
            break;
        }
    }
}



/**
 * Assumes a debug mode is present, and handles debug edit intro, debug stan edit, debug bond "view."
 * By default, the DEB_BOND_VIEW path is chosen without debug info.
 * This updates the player viewport(s), and handles player movement.
 *
 * Multiplayer:
 * Updates distance_traveled and possibly (depending on scenario) have_token_or_goldengun.
 *
 * US Address 0x7F0BF800.
 * EU address 7F0BEC44.
 */
void lvlViewMoveTick(void)
{
    s8 local_player_number;
    s32 padding;
    f32 temp_f0;
    f32 temp_f2;

    local_player_number = get_cur_playernum();
    cheat_buttons_mp_related();

    switch (get_debug_freeze_processing())
    {
        case 0:
        {
            if ((getDebugMode() == DEB_MOVE_VIEW) || ((getDebugMode() == DEB_INTRO_EDIT) && (D_80036ABC < 0)))
            {
                debugFreeCamera(joyGetStickX(local_player_number), joyGetStickY(local_player_number), joyGetButtons(local_player_number, ANY_BUTTON));
            }
            else
            {
                debugFreeCamera(joyGetStickX(local_player_number), joyGetStickY(local_player_number), 0);
            }
        }
        break;

        case 1:
        {
            if (getDebugMode() == DEB_STAN_VIEW)
            {
                debugStanView(joyGetStickX(local_player_number), joyGetStickY(local_player_number), joyGetButtons(local_player_number, ANY_BUTTON));
            }
            else
            {
                debugStanView(joyGetStickX(local_player_number), joyGetStickY(local_player_number), 0);
            }
        }
        break;

        case 2:
        {
            if (getDebugMode() == DEB_BOND_VIEW)
            {
                bondviewMovePlayerUpdateViewport(joyGetStickX(local_player_number), joyGetStickY(local_player_number), joyGetButtons(local_player_number, ANY_BUTTON));
            }
            else
            {
                bondviewMovePlayerUpdateViewport(joyGetStickX(local_player_number), joyGetStickY(local_player_number), 0);
            }

            mpwatchMenuTick();
        }
        break;
    }

    temp_f0 = g_CurrentPlayer->prop->pos.x - g_CurrentPlayer->bondprevpos.x;
    temp_f2 = g_CurrentPlayer->prop->pos.z - g_CurrentPlayer->bondprevpos.z;

    g_playerPerm->distance_traveled += sqrtf((temp_f0 * temp_f0) + (temp_f2 * temp_f2));

    if (get_scenario() == SCENARIO_TLD)
    {
        if (bondinvIsAliveWithFlag())
        {
            if (getCurrentPlayerWeaponId(GUNRIGHT) != ITEM_TOKEN)
            {
                currentPlayerEquipWeaponWrapper(GUNRIGHT, ITEM_TOKEN);

                if(1);

                if (g_CurrentPlayer->hands[GUNRIGHT].weapon_action_state == GUN_ANIM_STATE_FIRE)
                {
                    g_CurrentPlayer->hands[GUNRIGHT].weapon_action_state = GUN_ANIM_STATE_SWITCH_LOWER;
                }
            }

            g_playerPerm->flag_counter += g_ClockTimer;
            g_playerPerm->have_token_or_goldengun = 1;
        }
        else
        {
            g_playerPerm->have_token_or_goldengun = 0;
        }

        return;
    }

    if (get_scenario() == SCENARIO_MWTGG)
    {
        if (bondinvHasGoldenGun())
        {
            g_playerPerm->have_token_or_goldengun = 1;
        }
        else
        {
            g_playerPerm->have_token_or_goldengun = 0;
        }

        return;
    }
}





void lvlUnloadStageTextData(void)
{
    if (g_MpSoundStateRelated != NULL)
    {
        if (sndGetPlayingState(g_MpSoundStateRelated) != AL_STOPPED)
        {
            sndDeactivate(g_MpSoundStateRelated);
        }
    }

    if (g_CurrentStageToLoad != LEVELID_TITLE)
    {
        langClearBank(langGetLangBankIndexFromStagenum(g_CurrentStageToLoad));
        set_favorite_weapon_for_every_player();
    }

    cheatDisableAllCheats();
    cleanupGuardData();
    cleanupObjectSounds();
    cleanupExplosions();
    cleanup_window_pieces();
    cleanup_REMOVED_();
    cleanupAlarms();
    cleanupObjects(g_CurrentStageToLoad);
    cleanupObjectives();
    cleanupSFXRelated();
    cleanupplayersoundrelated();
    set_missionstate_zero();
    cleanup_rooms();
}


void lvlSetControlsLockedFlag(s32 arg0)
{
    #if defined(BUGFIX_R1)
    if ((arg0 != 0) && (g_ControlsLockedFlag == 0))
    {
        joyRumblePakStop();
    }
    #endif

    g_ControlsLockedFlag = arg0;
}


s32 lvlGetControlsLockedFlag(void)
{
    return g_ControlsLockedFlag;
}


DIFFICULTY lvlGetSelectedDifficulty(void)
{
    return g_SelectedDifficulty;
}


void lvlSetSelectedDifficulty(DIFFICULTY arg0)
{
    g_SelectedDifficulty = arg0;
}

void lvlSetMpTime(s32 arg0)
{
    g_MpTime = arg0;
}


void lvlSetMpPoint(s32 arg0)
{
    g_MpPoint = arg0;
}


f32 lvlGetCurrentMultiPlayerSec(void)
{
    return g_CurrentMultiPlayerSec;
}


f32 lvlGetCurrentMultiPlayerMin(void)
{
    return g_CurrentMultiPlayerMin;
}


/**
 * Unreferenced.
 */
f32 lvlGetStageTimeSec(void)
{
    return g_StageTimeSec;
}


/**
 * Unreferenced.
 */
f32 lvlGetPowerOnTimeSec(void)
{
    return g_PowerOnTimeSec;
}



#ifdef SL_ROMDBG_SIZECTL
/* The CONTROL for SIGHTLINE_ROM_DEBUG. Inert padding, no code, roughly the
 * size the debug mailbox adds.
 *
 * boss.c:389 seeds the RNG with osGetCount(), the CPU cycle counter, so any
 * ROM whose image differs at all boots with a different seed and diverges from
 * the parity trace on rng_seed alone. A ROM built with THIS flag is an
 * unmodified game that differs from the parity image by a comparable amount.
 * If it diverges the same way the debug ROM does, the divergence belongs to
 * the image, not to the instrument - which is the only way to read the debug
 * ROM's trace comparison honestly. `const` so it lands in rodata and cannot be
 * dead-stripped into nothing. */
const u32 sl_romdbg_sizectl[0x230 / 4] = { 0xC0FFEE00 };
#endif

#ifdef SIGHTLINE_ROM_DEBUG
/* ===========================================================================
 * SIGHTLINE_ROM_DEBUG - the debug mailbox. See src/game/sl_romdbg.h for why
 * this exists, what the rules are, and why the trigger is a mailbox word
 * rather than a button.
 *
 * This lives in lv.c rather than a file of its own ON PURPOSE. A new .c file
 * is a new object in the link, and the matching build's layout is exactly what
 * `make matching` compares byte for byte. Appending an #ifdef'd block to a file
 * already in the link cannot move a single byte of the matching ROM, because
 * the preprocessor deletes all of it.
 * ===========================================================================
 */
#include "matrixmath.h"
#include "sl_romdbg.h"

/* Declared in propobj.c, not in a header: the game's own per-prop hit test,
 * the one a bullet uses. Reusing it is the whole point - a second raycaster
 * would be a second instrument that can disagree with the first. */
void sub_GAME_7F04E9BC(PropRecord *prop, struct ShotData *shotdata);

SlRomDbg g_SlRomDbg;

/* Read the host's half of the mailbox, which was written behind the CPU's
 * back. Without the invalidate this is served from the data cache and the
 * command is never seen - a silent null, which is the failure mode this
 * project has paid for five times over. */
static void slRomDbgHostRead(u32 *seq, u32 *cmd, u32 *arg)
{
    osInvalDCache(&g_SlRomDbg, sizeof(g_SlRomDbg));
    *seq = g_SlRomDbg.host_seq;
    *cmd = g_SlRomDbg.host_cmd;
    *arg = g_SlRomDbg.host_arg;
}

static void slRomDbgPublish(void)
{
    g_SlRomDbg.rom_seq++;
    osWritebackDCache(&g_SlRomDbg, sizeof(g_SlRomDbg));
}

/* A prop pointer is only as good as the frame it was captured on: props are
 * freed and their slots reused. The back-pointer is the cheapest check the
 * data itself offers, and it is the one the game's own code relies on. */
static s32 slRomDbgPropAlive(PropRecord *prop)
{
    ObjectRecord *obj;

    if (prop == NULL)
    {
        return 0;
    }

    if (prop->type != PROP_TYPE_OBJ && prop->type != PROP_TYPE_WEAPON
        && prop->type != PROP_TYPE_DOOR)
    {
        return 0;
    }

    obj = prop->obj;

    if (obj == NULL || obj->prop != prop)
    {
        return 0;
    }

    /* The back-pointer alone is not identity - it passes for whoever occupies
     * the slot next. Pin the object and its PROPDEF at selection time and
     * insist on both. See the comment on watch_obj_sel in sl_romdbg.h. */
    if ((u32) obj != g_SlRomDbg.watch_obj_sel
        || obj->type != g_SlRomDbg.watch_objtype_sel)
    {
        return 0;
    }

    return 1;
}

/* One place that starts watching, so the identity fields can never be set by
 * one path and forgotten by another. */
static void slRomDbgWatch(PropRecord *prop)
{
    g_SlRomDbg.watch_prop = (u32) prop;
    g_SlRomDbg.watch_obj_sel = (prop != NULL) ? (u32) prop->obj : 0;
    g_SlRomDbg.watch_objtype_sel =
        (prop != NULL && prop->obj != NULL) ? prop->obj->type : -1;
}

/*
 * Where the crosshair points, in world space.
 *
 * This is the deterministic half of bullet_path_from_screen_center(): the
 * crosshair angle put through the same transform, WITHOUT the four
 * RANDOMFRAC() calls that function makes to apply weapon spread. The RNG
 * cursor is hashed by the trace, so consuming it would move the simulation -
 * the one thing an oracle may never do. Dropping the spread also gives the
 * crosshair CENTRE rather than a spread-perturbed sample of it, which is what
 * "what am I aiming at" means.
 *
 * Published on every record, not only on a SELECT: a per-frame sample whose
 * aim vector reads (0,0,0) invites exactly the kind of inference this whole
 * instrument exists to remove.
 */
static void slRomDbgAim(coord3d *origin, coord3d *dir)
{
    coord2d crosspos;
    Mtxf *viewtoworld;

    crosspos.x = g_CurrentPlayer->crosshair_angle.f[0];
    crosspos.y = g_CurrentPlayer->crosshair_angle.f[1];

    origin->x = 0.0f;
    origin->y = 0.0f;
    origin->z = 0.0f;
    transformAndNormalizeByLength2Dto3D(&crosspos, dir, 1.0f);

    viewtoworld = currentPlayerGetViewToWorldMtxf();
    mtx4TransformVecInPlace(viewtoworld, origin);
    mtx4RotateVecInPlace(viewtoworld, dir);
}

/* The gate at the top of sub_GAME_7F04E9BC(). A prop that fails it is on
 * screen and still invisible to every shot, which is exactly the thing that
 * cannot be inferred from "the pane is drawn". Reported, not assumed. */
static s32 slRomDbgShotGate(PropRecord *prop)
{
    ObjectRecord *obj = prop->obj;

    if (obj == NULL)
    {
        return 0;
    }

    return ((prop->flags & PROPFLAG_ONSCREEN) != 0)
        && ((obj->runtime_bitflags & RUNTIMEBITFLAG_00001000) == 0)
        && ((obj->flags2 & PROPFLAG2_SHOOTTHROUGH) == 0);
}

/*
 * FINDTYPE: every on-screen prop of one PROPDEF type, nearest first.
 *
 * The diagnostic half of the instrument. SELECT answering MISS is ambiguous -
 * the crosshair may be off the pane, or no pane may be on screen, or the pane
 * may be failing the shot gate. This separates those three, using the same
 * prop list, so a null SELECT becomes a measurement instead of a shrug.
 *
 * `dowatch` is 0 for SL_DBG_CMD_NEAREST, which the live recorder issues on its
 * own initiative - the periodic aiming aid, and the follow-up that answers
 * "what WAS on screen" after a MISS. Neither may be able to replace the
 * identity a deliberate SELECT just pinned, so listing and taking over the
 * watch are separated rather than bundled together.
 */
static void slRomDbgFindType(s32 objtype, s32 dowatch)
{
    PropRecord **pp;
    PropRecord *prop;
    PropRecord *player;
    ObjectRecord *obj;
    f32 dx;
    f32 dy;
    f32 dz;
    f32 d;
    s32 n;
    s32 i;
    s32 j;
    s32 best;
    f32 bestd;

    n = 0;
    player = getCurrentPlayerProp();

    for (pp = g_LastOnScreenProp; (--pp) >= g_OnScreenPropList;)
    {
        prop = *pp;

        if (prop == NULL || n >= ARRAYCOUNT(g_SlRomDbg.hits))
        {
            continue;
        }

        if (prop->type != PROP_TYPE_OBJ && prop->type != PROP_TYPE_WEAPON
            && prop->type != PROP_TYPE_DOOR)
        {
            continue;
        }

        obj = prop->obj;

        /* objtype < 0 means "everything on screen". Without it, a FINDTYPE
         * that reports nothing cannot be told apart from a FINDTYPE aimed at
         * a PROPDEF this level never uses. */
        if (obj == NULL || (objtype >= 0 && obj->type != objtype))
        {
            continue;
        }

        d = 0.0f;

        if (player != NULL)
        {
            dx = obj->runtime_pos.x - player->pos.x;
            dy = obj->runtime_pos.y - player->pos.y;
            dz = obj->runtime_pos.z - player->pos.z;
            d = sqrtf(dx * dx + dy * dy + dz * dz);
        }

        g_SlRomDbg.hits[n].prop = (u32) prop;
        g_SlRomDbg.hits[n].obj = (u32) obj;
        g_SlRomDbg.hits[n].model = (u32) obj->model;
        g_SlRomDbg.hits[n].dist = d;
        g_SlRomDbg.hits[n].proptype = prop->type;
        g_SlRomDbg.hits[n].objtype = obj->type;
        g_SlRomDbg.hits[n].hitpart = prop->flags;
        g_SlRomDbg.hits[n].penetrates = slRomDbgShotGate(prop);
        n++;
    }

    g_SlRomDbg.nhits = n;
    g_SlRomDbg.event = SL_DBG_EV_FIND;

    /* Selection sort by distance: n is at most 10 and this runs only on a
     * command, so clarity beats cleverness. */
    for (i = 0; i < n; i++)
    {
        best = i;
        bestd = g_SlRomDbg.hits[i].dist;

        for (j = i + 1; j < n; j++)
        {
            if (g_SlRomDbg.hits[j].dist < bestd)
            {
                best = j;
                bestd = g_SlRomDbg.hits[j].dist;
            }
        }

        if (best != i)
        {
            SlDbgHit tmp = g_SlRomDbg.hits[i];
            g_SlRomDbg.hits[i] = g_SlRomDbg.hits[best];
            g_SlRomDbg.hits[best] = tmp;
        }
    }

    if (n > 0 && dowatch)
    {
        slRomDbgWatch((PropRecord *) g_SlRomDbg.hits[0].prop);
    }
}

/*
 * Run the game's targeting selection with the effects removed.
 *
 * This is a zero-damage shot: same crosshair, same on-screen prop list, same
 * per-prop hit test, same candidate sorting. What it does NOT do:
 *
 *  - it does not call bullet_path_from_screen_center(). That function calls
 *    RANDOMFRAC() four times to apply weapon spread, and the RNG cursor is
 *    hashed by the trace. Calling it would move the simulation, which is the
 *    one thing an oracle may never do. The deterministic half - the crosshair
 *    angle put through transformAndNormalizeByLength2Dto3D() - is reproduced
 *    here verbatim, which also gives the crosshair CENTRE rather than a
 *    spread-perturbed sample of it. That is what "what am I aiming at" means.
 *
 *  - it does not walk stan tiles or test the background, so it never calls
 *    stanResetHits(). maxdist therefore stays unbounded and a prop behind a
 *    wall can be reported. The full candidate list is published, nearest
 *    first, so that is visible rather than hidden.
 *
 *  - it does not test characters. chrTestHit() is not reused because its side
 *    effects are not established, and glass is never a PROP_TYPE_CHR.
 */
static void slRomDbgSelect(void)
{
    ShotData shotdata;
    coord2d crosspos;
    PropRecord **pp;
    PropRecord *prop;
    Mtxf *viewtoworld;
    s32 k;
    s32 i;
    s32 best;
    f32 bestdist;

    g_SlRomDbg.nhits = 0;

    for (k = 0; k < ARRAYCOUNT(g_SlRomDbg.hits); k++)
    {
        g_SlRomDbg.hits[k].prop = 0;
        g_SlRomDbg.hits[k].obj = 0;
        g_SlRomDbg.hits[k].model = 0;
        g_SlRomDbg.hits[k].dist = 0.0f;
        g_SlRomDbg.hits[k].proptype = -1;
        g_SlRomDbg.hits[k].objtype = -1;
        g_SlRomDbg.hits[k].hitpart = 0;
        g_SlRomDbg.hits[k].penetrates = 0;
    }

    for (k = 0; k < ARRAYCOUNT(shotdata.hits); k++)
    {
        shotdata.hits[k].prop = NULL;
        shotdata.hits[k].hitpart = 0;
        shotdata.hits[k].node = NULL;
        shotdata.hits[k].dist = 0.0f;
        shotdata.hits[k].countsAsPenetration = 0;
    }

    slRomDbgAim(&shotdata.viewOrigin, &shotdata.viewDir);

    shotdata.weapon = getCurrentPlayerWeaponId(GUNRIGHT);
    shotdata.maxdist = M_U32_MAX_VALUE_F;

    /* viewOrigin/viewDir are VIEW space, which is what sub_GAME_7F04E9BC
     * expects; gunpos/dir are the same ray in world space, which is what the
     * report needs. slRomDbgAim gives the world-space pair, so re-derive the
     * view-space pair from the crosshair before transforming. */
    crosspos.x = g_CurrentPlayer->crosshair_angle.f[0];
    crosspos.y = g_CurrentPlayer->crosshair_angle.f[1];
    shotdata.viewOrigin.x = 0.0f;
    shotdata.viewOrigin.y = 0.0f;
    shotdata.viewOrigin.z = 0.0f;
    transformAndNormalizeByLength2Dto3D(&crosspos, &shotdata.viewDir, 1.0f);

    shotdata.gunpos.x = shotdata.viewOrigin.x;
    shotdata.gunpos.y = shotdata.viewOrigin.y;
    shotdata.gunpos.z = shotdata.viewOrigin.z;
    shotdata.dir.x = shotdata.viewDir.x;
    shotdata.dir.y = shotdata.viewDir.y;
    shotdata.dir.z = shotdata.viewDir.z;
    viewtoworld = currentPlayerGetViewToWorldMtxf();
    mtx4TransformVecInPlace(viewtoworld, &shotdata.gunpos);
    mtx4RotateVecInPlace(viewtoworld, &shotdata.dir);

    g_SlRomDbg.weapon = shotdata.weapon;

    for (pp = g_LastOnScreenProp; (--pp) >= g_OnScreenPropList;)
    {
        prop = *pp;

        if (prop == NULL)
        {
            continue;
        }

        if (prop->type == PROP_TYPE_OBJ || prop->type == PROP_TYPE_WEAPON
            || prop->type == PROP_TYPE_DOOR)
        {
            sub_GAME_7F04E9BC(prop, &shotdata);
        }
    }

    /* Publish every candidate, nearest first, so "it picked the wrong pane"
     * is answerable without re-running anything. */
    i = 0;

    for (k = 0; k < ARRAYCOUNT(shotdata.hits); k++)
    {
        if (shotdata.hits[k].prop != NULL)
        {
            g_SlRomDbg.hits[i].prop = (u32) shotdata.hits[k].prop;
            g_SlRomDbg.hits[i].obj = (u32) shotdata.hits[k].prop->obj;
            g_SlRomDbg.hits[i].model = (u32) shotdata.hits[k].model;
            g_SlRomDbg.hits[i].dist = shotdata.hits[k].dist;
            g_SlRomDbg.hits[i].proptype = shotdata.hits[k].prop->type;
            g_SlRomDbg.hits[i].objtype =
                (shotdata.hits[k].prop->obj != NULL)
                    ? shotdata.hits[k].prop->obj->type : -1;
            g_SlRomDbg.hits[i].hitpart = shotdata.hits[k].hitpart;
            g_SlRomDbg.hits[i].penetrates = shotdata.hits[k].countsAsPenetration;
            i++;
        }
    }

    g_SlRomDbg.nhits = i;

    /* Sort by insertion order is not distance order: chrpropAddBulletHit
     * fills whichever slot is free. Pick the nearest explicitly. */
    best = -1;
    bestdist = 0.0f;

    for (k = 0; k < i; k++)
    {
        if (best < 0 || g_SlRomDbg.hits[k].dist < bestdist)
        {
            best = k;
            bestdist = g_SlRomDbg.hits[k].dist;
        }
    }

    if (best < 0)
    {
        g_SlRomDbg.event = SL_DBG_EV_MISS;
        return;
    }

    slRomDbgWatch((PropRecord *) g_SlRomDbg.hits[best].prop);
    g_SlRomDbg.event = SL_DBG_EV_SELECT;
}

/* Everything the host cannot get by walking pointers with confidence: the
 * fields whose MIPS offsets the compiler knows and a host-side decoder would
 * have to guess. Pointers are published too, so the host can walk further. */
static void slRomDbgSampleWatched(void)
{
    PropRecord *prop;
    ObjectRecord *obj;
    struct TintedGlassRecord *glass;
    PropRecord *player;
    PropRecord **pp;
    f32 dx;
    f32 dy;
    f32 dz;
    s32 k;

    prop = (PropRecord *) g_SlRomDbg.watch_prop;

    if (!slRomDbgPropAlive(prop))
    {
        /* Say so rather than publishing a stale record. A watched prop that
         * was freed, or whose slot has been reused by a different object, must
         * read as GONE - not as frozen, and above all not as still being the
         * thing that was selected. */
        g_SlRomDbg.event = SL_DBG_EV_LOST;
        g_SlRomDbg.watch_prop = 0;
        g_SlRomDbg.watch_obj = 0;
        g_SlRomDbg.watch_model = 0;
        g_SlRomDbg.watch_objtype = -1;
        g_SlRomDbg.watch_proptype = -1;
        return;
    }

    obj = prop->obj;

    g_SlRomDbg.watch_obj = (u32) obj;
    g_SlRomDbg.watch_model = (u32) obj->model;
    g_SlRomDbg.watch_objtype = obj->type;
    g_SlRomDbg.watch_proptype = prop->type;
    g_SlRomDbg.watch_propflags = prop->flags;
    g_SlRomDbg.watch_zdepth = prop->zDepth;
    g_SlRomDbg.watch_pos[0] = prop->pos.x;
    g_SlRomDbg.watch_pos[1] = prop->pos.y;
    g_SlRomDbg.watch_pos[2] = prop->pos.z;
    g_SlRomDbg.watch_runtime_pos[0] = obj->runtime_pos.x;
    g_SlRomDbg.watch_runtime_pos[1] = obj->runtime_pos.y;
    g_SlRomDbg.watch_runtime_pos[2] = obj->runtime_pos.z;
    g_SlRomDbg.watch_objflags = obj->flags;
    g_SlRomDbg.watch_objflags2 = obj->flags2;
    g_SlRomDbg.watch_runtime_bitflags = obj->runtime_bitflags;
    g_SlRomDbg.watch_damage = obj->damage;
    g_SlRomDbg.watch_maxdamage = obj->maxdamage;

    for (k = 0; k < PROPRECORD_STAN_ROOM_LEN; k++)
    {
        g_SlRomDbg.watch_rooms[k] = prop->rooms[k];
    }

    g_SlRomDbg.watch_stan_room = (prop->stan != NULL) ? getTileRoom(prop->stan) : -1;

    /* Is it in the list the renderer and the shot both walk? PROPFLAG_ONSCREEN
     * and list membership are set by different code, so record both rather
     * than assuming one implies the other. */
    g_SlRomDbg.watch_onscreen_listed = 0;

    for (pp = g_LastOnScreenProp; (--pp) >= g_OnScreenPropList;)
    {
        if (*pp == prop)
        {
            g_SlRomDbg.watch_onscreen_listed = 1;
            break;
        }
    }

    g_SlRomDbg.glass_tintdist = 0;
    g_SlRomDbg.glass_culldist = 0;
    g_SlRomDbg.glass_opacity = -1;
    g_SlRomDbg.glass_portalnum = -1;
    g_SlRomDbg.glass_unk90 = 0.0f;
    g_SlRomDbg.portal_ctrl = -1;
    g_SlRomDbg.portal_room1 = -1;
    g_SlRomDbg.portal_room2 = -1;

    if (obj->type == PROPDEF_TINTED_GLASS)
    {
        glass = (struct TintedGlassRecord *) obj;
        g_SlRomDbg.glass_tintdist = glass->TintDist;
        g_SlRomDbg.glass_culldist = glass->CullDist;
        g_SlRomDbg.glass_opacity = glass->calculatedopacity;
        g_SlRomDbg.glass_portalnum = glass->portalnum;
        g_SlRomDbg.glass_unk90 = glass->unk90;

        if (glass->portalnum >= 0)
        {
            g_SlRomDbg.portal_ctrl = g_BgPortals[glass->portalnum].controlbytes1;
            g_SlRomDbg.portal_room1 = g_BgPortals[glass->portalnum].connectedRoom1;
            g_SlRomDbg.portal_room2 = g_BgPortals[glass->portalnum].connectedRoom2;
        }
    }

    player = getCurrentPlayerProp();

    if (player != NULL)
    {
        g_SlRomDbg.player_pos[0] = player->pos.x;
        g_SlRomDbg.player_pos[1] = player->pos.y;
        g_SlRomDbg.player_pos[2] = player->pos.z;
        g_SlRomDbg.player_room = (player->stan != NULL) ? getTileRoom(player->stan) : -1;
        dx = obj->runtime_pos.x - player->pos.x;
        dy = obj->runtime_pos.y - player->pos.y;
        dz = obj->runtime_pos.z - player->pos.z;
        g_SlRomDbg.watch_dist = sqrtf(dx * dx + dy * dy + dz * dz);
    }
}

/*
 * Once per frame, from lv.c's in-game path.
 *
 * hook_calls advances unconditionally. That is the known-positive control: an
 * instrument that cannot fail is worthless, and a mailbox whose hook never ran
 * is indistinguishable from a mailbox with nothing to say unless something
 * moves every single frame.
 */
void slRomDbgTick(void)
{
    u32 seq;
    u32 cmd;
    u32 arg;
    s32 count;
    PropRecord **pp;

    if (g_SlRomDbg.magic != SL_ROMDBG_MAGIC)
    {
        bzero(&g_SlRomDbg, sizeof(g_SlRomDbg));
        g_SlRomDbg.magic = SL_ROMDBG_MAGIC;
        g_SlRomDbg.version = SL_ROMDBG_VERSION;
        g_SlRomDbg.size = sizeof(g_SlRomDbg);
        g_SlRomDbg.glass_opacity = -1;
        g_SlRomDbg.glass_portalnum = -1;
    }

    g_SlRomDbg.hook_calls++;
    g_SlRomDbg.frame = currentFrameCounter;
    g_SlRomDbg.event = SL_DBG_EV_NONE;

    count = 0;

    for (pp = g_LastOnScreenProp; (--pp) >= g_OnScreenPropList;)
    {
        if (*pp != NULL)
        {
            count++;
        }
    }

    g_SlRomDbg.onscreen_count = count;

    {
        coord3d aimorigin;
        coord3d aimdir;

        slRomDbgAim(&aimorigin, &aimdir);
        g_SlRomDbg.aim_origin[0] = aimorigin.x;
        g_SlRomDbg.aim_origin[1] = aimorigin.y;
        g_SlRomDbg.aim_origin[2] = aimorigin.z;
        g_SlRomDbg.aim_dir[0] = aimdir.x;
        g_SlRomDbg.aim_dir[1] = aimdir.y;
        g_SlRomDbg.aim_dir[2] = aimdir.z;
        g_SlRomDbg.weapon = getCurrentPlayerWeaponId(GUNRIGHT);
    }

    /* Is there a crosshair on screen at all? Published every frame, not only
     * on a SELECT, because it is what tells "I aimed at the wrong place" apart
     * from "I was not aiming". These are the two words gunDrawSight() itself
     * tests (gunfire.c:6276), so this is the crosshair rather than a proxy. */
    g_SlRomDbg.sight_mode = g_CurrentPlayer->gunsightmode;
    g_SlRomDbg.sight_mp_menu = g_CurrentPlayer->mpmenuon;

    slRomDbgHostRead(&seq, &cmd, &arg);

    if (seq != g_SlRomDbg.rom_ack)
    {
        g_SlRomDbg.rom_ack = seq;

        if (cmd == SL_DBG_CMD_SELECT)
        {
            slRomDbgSelect();
        }
        else if (cmd == SL_DBG_CMD_CLEAR)
        {
            slRomDbgWatch(NULL);
            g_SlRomDbg.nhits = 0;
        }
        else if (cmd == SL_DBG_CMD_SNAPSHOT)
        {
            g_SlRomDbg.event = SL_DBG_EV_SAMPLE;
        }
        else if (cmd == SL_DBG_CMD_FINDTYPE)
        {
            slRomDbgFindType((s32) arg, 1);
        }
        else if (cmd == SL_DBG_CMD_NEAREST)
        {
            /* Same listing, no change of watch, and its own event id so the
             * host can never mistake an aid's automatic peek for a FINDTYPE a
             * human asked for. */
            slRomDbgFindType((s32) arg, 0);
            g_SlRomDbg.event = SL_DBG_EV_NEAR;
        }
    }

    if (g_SlRomDbg.watch_prop != 0)
    {
        slRomDbgSampleWatched();
    }

    slRomDbgPublish();
}
#endif /* SIGHTLINE_ROM_DEBUG */
