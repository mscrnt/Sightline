#include <ultra64.h>
#include "front.h"
#include <bondconstants.h>
 /**
  * initGameData
  *
  **/
void initGameData(void) {
    current_menu = MENU_INVALID;
    menu_update = MENU_LEGAL_SCREEN;
    maybe_prev_menu = MENU_INVALID;
    g_MenuTimer = 0;
    selected_stage = LEVELID_NONE;
    briefingpage = BRIEFING_INVALID;
    selected_difficulty = DIFFICULTY_MULTI;
    screen_size = SCREEN_SIZE_320x240;
    folder_selection_screen_option_icon = 0;
    selected_folder_num = FOLDER1;
    mission_failed_or_aborted = FALSE;
    is_first_time_on_legal_screen = TRUE;
    is_first_time_on_main_menu = TRUE;
    prev_keypresses = FALSE;
    maybe_is_in_menu = TRUE;
    // Sightline: 007 mode is the CONFIGURABLE difficulty - get_007_*_mod()
    // reads these sliders, and their defaults (1/1/1/0) are what make an
    // unconfigured 007 measure the same as 00 Agent. That is the defaults
    // coinciding, not the difficulties being equivalent.
    //
    // Overridden HERE rather than at the definition in front.c: this function
    // reassigns them at boot, so a changed initialiser is simply overwritten -
    // the build differs, the ROM differs, and the values still read 1/1/1/0.
    //
    // The menu computes them as x*x*10 for x in 0..1, so accuracy, damage and
    // health range 0..10 and reaction 0..1. chraction.c branches differently
    // above 1.0, so a maxed 007 reaches guard behaviour no fixed difficulty
    // offers.
    //
    // Undefined by default; the matching build is unaffected.
#ifdef SL_007_REACTION
    slider_007_mode_reaction = SL_007_REACTION;
#else
    slider_007_mode_reaction = 0.0f;
#endif
#ifdef SL_007_HEALTH
    slider_007_mode_health = SL_007_HEALTH;
#else
    slider_007_mode_health = 1.0f;
#endif
#ifdef SL_007_DAMAGE
    slider_007_mode_damage = SL_007_DAMAGE;
#else
    slider_007_mode_damage = 1.0f;
#endif
#ifdef SL_007_ACCURACY
    slider_007_mode_accuracy = SL_007_ACCURACY;
#else
    slider_007_mode_accuracy = 1.0f;
#endif
#ifndef __sgi
    /* native runtime equivalents of the SL_007_* build defines, applied at
     * the same point so replay parameters come from the environment instead
     * of a per-configuration binary (sl_env_f32 lives in the shim) */
    {
        extern float sl_env_f32(const char *name, float dflt);
        slider_007_mode_reaction = sl_env_f32("SL_007_REACTION", slider_007_mode_reaction);
        slider_007_mode_health   = sl_env_f32("SL_007_HEALTH",   slider_007_mode_health);
        slider_007_mode_damage   = sl_env_f32("SL_007_DAMAGE",   slider_007_mode_damage);
        slider_007_mode_accuracy = sl_env_f32("SL_007_ACCURACY", slider_007_mode_accuracy);
    }
#endif
    intro_character_index = 0;
    randomly_selected_intro_animation = ANIM_idle;
    intro_animation_count = 0;
    cast_model = NULL;
    cast_model_weapon = NULL;
    full_actor_intro = FALSE;
#ifdef DEBUG
    load_body_head_if_not_loaded(BODY_Brosnan_Tuxedo, "char");
    load_body_head_if_not_loaded(BODY_Male_Pierce_Bond_Tuxedo, "head");
    LoadItemModel(PROP_GOLDENEYELOGO);
    LoadItemModel(PROP_LEGALPAGE);
#endif
}


