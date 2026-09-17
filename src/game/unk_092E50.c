#include <ultra64.h>
#include "bondview.h"
#include "lv.h"
#include "unk_092E50.h"

// bss
//CODE.bss:80079E80
f32 flt_CODE_bss_80079E80;
//CODE.bss:80079E84
f32 flt_CODE_bss_80079E84;
//CODE.bss:80079E88
f32 flt_CODE_bss_80079E88;


// data
//D:8003FCC0
Gfx MipMap2C_Something_Setup[] = {
    gsDPSetTile(G_IM_FMT_I, G_IM_SIZ_4b, 4, 0, 0, 0, G_TX_WRAP, 6, 0, G_TX_WRAP, 6, 0),
    gsDPSetTile(G_IM_FMT_I, G_IM_SIZ_4b, 4, 0, 1, 0, G_TX_WRAP, 6, 0, G_TX_WRAP, 6, 0),
    gsDPSetTileSize(0, 2, 2, 0, 0),
    gsDPSetTileSize(1, 2, 2, 0, 0),
    gsDPSetPrimColor(0, 15, 255, 255, 255, 255),
    gsDPSetTextureDetail(G_TD_CLAMP),
    gsDPSetTextureFilter(G_TF_BILERP),
    gsDPSetCombineLERP(TEXEL1, TEXEL0, PRIM_LOD_FRAC, TEXEL0,  TEXEL1, TEXEL0, PRIM_LOD_FRAC, TEXEL0,  COMBINED, 0, SHADE, 0,  COMBINED, 0, SHADE, 0),
    gsDPSetRenderMode(G_RM_PASS, G_RM_AA_ZB_OPA_SURF2),
    gsDPSetTextureLOD(G_TL_TILE),
    gsDPSetCycleType(G_CYC_2CYCLE),
    gsSPSetGeometryMode(G_CULL_BACK ),
    gsSPEndDisplayList()
};

//D:8003FD28
Gfx MipMap2C_Something2_Setup[] = {
    gsDPSetTile(G_IM_FMT_CI, G_IM_SIZ_8b, 2, 0, 0, 0, G_TX_WRAP, 5, 0, G_TX_WRAP, 5, 0),
    gsDPSetTile(G_IM_FMT_CI, G_IM_SIZ_8b, 2, 0, 1, 0, G_TX_WRAP, 5, 0, G_TX_WRAP, 5, 0),
    gsDPSetTileSize(0, 2, 2, 0, 0),
    gsDPSetTileSize(1, 2, 2, 0, 0),
    gsDPSetPrimColor(0, 15, 255, 255, 255, 255),
    gsDPSetTextureDetail(G_TD_CLAMP),
    gsDPSetTextureFilter(G_TF_BILERP),
    gsDPSetCombineLERP(TEXEL1, TEXEL0, PRIM_LOD_FRAC, TEXEL0,  TEXEL1, TEXEL0, PRIM_LOD_FRAC, TEXEL0,  COMBINED, 0, SHADE, 0,  COMBINED, 0, SHADE, 0),
    gsDPSetRenderMode(G_RM_PASS, G_RM_AA_ZB_OPA_SURF2),
    gsDPSetTextureLOD(G_TL_TILE),
    gsDPSetCycleType(G_CYC_2CYCLE),
    gsSPSetGeometryMode(G_CULL_BACK ),
    gsSPEndDisplayList()
};

//End Dl means this gfx list cannot go any further. perhaps below is a vtx array?

u32 D_8003FD90 = 0;
f32 g_SkyCloudOffset = 0;
f32 D_8003FD98[] = { 0, 0 };

struct hand D_8003FDA0 = {
    0, /* weaponnum */
    -1, /* weaponnum_watchmenu */
    0, /* previous_weapon */
    0, /* weapon_firing_status */
    0, /* field_87D */
    1, /* field_87E */
    0, /* field_87F */
    0, /* weapon_hold_time */
    0, /* field_884 */
    0, /* field_888 */
    0, /* field_88C */
    0, /* field_890 */
    0, /* weapon_action_state */
    0, /* weapon_current_animation */
    0, /* weapon_ammo_in_magazine */
    0, /* field_8A0 */
    0, /* numvisibleshells */
    0, /* field_8A8 */
    0, /* weapon_next_weapon */
    0, /* field_8B0 */
    0, /* weapon_animation_trigger */
    0, /* field_8B8 */
    0, /* field_8BC */
    0, /* field_8C0 */
    0, /* field_8C4 */
    0, /* field_8C8 */
    0, /* field_8CC */
    0, /* field_8D0 */
    0, /* field_8D4 */
    0, /* field_8D8 */
    0, /* field_8DC */
    0, /* field_8E0 */
    0, /* field_8E4 */
    0, /* field_8E8 */
    { { {1.0f,0.0f,0.0f,0.0f}, {0.0f,1.0f,0.0f,0.0f}, {0.0f,0.0f,1.0f,0.0f}, {0.0f,0.0f,0.0f,1.0f} } }, /* field_8EC (identity) */
    0, /* field_92C */
    0, /* sway_pos_x */
    0, /* sway_pos_y */
    0, /* sway_pos_z */
    0, /* sway_look_x */
    0, /* sway_look_y */
    -1.0f, /* sway_look_z */
    0, /* sway_up_x */
    1.0f, /* sway_up_y */
    0, /* sway_up_z */
    0, /* spring_pos_x */
    0, /* spring_pos_y */
    0, /* spring_pos_z */
    0, /* spring_look_x */
    0, /* spring_look_y */
    /* spring_look_z = 1 / GUN_SPRING_SCALE, so the derived sway vector starts unit-length */
#if defined(BUGFIX_R2)
    -16.7504158f,
#else
    -19.999996f,
#endif
    0, /* spring_up_x */
    /* spring_up_y = 1 / GUN_SPRING_SCALE */
#if defined(BUGFIX_R2)
    16.7504158f,
#else
    19.999996f,
#endif
    0, /* spring_up_z */
    { {{0,0,0}}, {{0,0,0}}, {{0,0,0}}, {{0,0,0}} }, /* blendpos[4] */
    { {{0,0,-1.0f}}, {{0,0,-1.0f}}, {{0,0,-1.0f}}, {{0,0,-1.0f}} }, /* blendlook[4] */
    { {{0,1.0f,0}}, {{0,1.0f,0}}, {{0,1.0f,0}}, {{0,1.0f,0}} }, /* blendup[4] */
    0, /* curblendpos */
    0, /* dampt */
    1.0f, /* blendscale */
    1.0f, /* blendscale1 */
    0, /* sideflag */
    0, /* weapon_theta_displacement */
    0, /* weapon_verta_displacement */
    0, /* field_A24 */
    0, /* gunofs2_x */
    0, /* gunofs2_y */
    0, /* gunofs2_z */
    0, /* field_A34 */
    0, /* field_A38 */
    0, /* field_A3C */
    1000.0f, /* field_A40 */
    0, /* audioHandle */
    0, /* field_A48 */
    0, /* field_A4C */
    0, /* field_A50 */
    { -1, 0, 0, 0, {{0,0,0}}, {{0,0,0}}, 0.0f, 0.0f, 0.0f, 0.0f }, /* weapon_beam */
    0, /* noise */
    0, /* field_A84 */
    0, /* field_A88 */
    0, /* field_A8C */
    0, /* rocket */
    0, /* firedrocket */
    0, /* gunmtx_camspace.m[0][0] */
    0, /* gunmtx_camspace.m[0][1] */
    0, /* gunmtx_camspace.m[0][2] */
    0, /* gunmtx_camspace.m[0][3] */
    0, /* gunmtx_camspace.m[1][0] */
    0, /* gunmtx_camspace.m[1][1] */
    0, /* gunmtx_camspace.m[1][2] */
    0, /* gunmtx_camspace.m[1][3] */
    0, /* gunmtx_camspace.m[2][0] */
    0, /* gunmtx_camspace.m[2][1] */
    0, /* gunmtx_camspace.m[2][2] */
    0, /* gunmtx_camspace.m[2][3] */
    0, /* gunmtx_camspace.m[3][0] */
    0, /* gunmtx_camspace.m[3][1] */
    0, /* gunmtx_camspace.m[3][2] */
    0, /* gunmtx_camspace.m[3][3] */
    { { {0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0} } }, /* throw_item_pos_related */
    { { {0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0} } }, /* throw_item_pos_related_prev */
    { {0,0,0} }, /* field_B58 */
    0, /* field_B64 */
    0, /* field_B68 */
    0, /* field_B6C */
    0, /* field_B70 */
    0, /* mtxlist */
    0, /* field_B78 */
    0, /* field_B7C */
    0, /* field_B80 */
    0, /* field_B84 */
    0, /* modeldatas */
    0, /* field_B8C */
    0, /* field_B90 */
    0, /* field_B94 */
    0, /* field_B98 */
    0, /* field_B9C */
    0, /* field_BA0 */
    0, /* field_BA4 */
    0, /* field_BA8 */
    0, /* field_BAC */
    0, /* field_BB0 */
    0, /* field_BB4 */
    0, /* field_BB8 */
    0, /* field_BBC */
    0, /* field_BC0 */
    0, /* field_BC4 */
    0, /* field_BC8 */
    0, /* field_BCC */
    0, /* field_BD0 */
    0, /* field_BD4 */
    0, /* field_BD8 */
    0, /* field_BDC */
    0, /* field_BE0 */
    0, /* field_BE4 */
    0, /* field_BE8 */
    0, /* field_BEC */
    0, /* field_BF0 */
    0, /* field_BF4 */
    0, /* field_BF8 */
    0, /* field_BFC */
    0, /* field_C00 */
    0, /* field_C04 */
    0, /* volley */
    { {0,0,0} }, /* item_related */
};


u64 D_80040148[] = { 0, 0, 0 }; // Unused.


#ifndef __sgi
/* The upper-left corner of an F2 rdp_settilesize, written the way the RDP
 * reads it rather than the way a big-endian compiler happens to lay the
 * bitfields out.
 *
 * Gloadtile (include/PR/gbi.h:1681) is declared `int cmd:8; unsigned sl:12;
 * unsigned tl:12;`.  That declaration only describes the RDP command word
 * when the compiler allocates bitfields from the most significant bit down,
 * which IDO on MIPS does and i686-w64-mingw32-gcc does not.  Nothing in
 * gbi.h switches the declaration on endianness - `grep -n IS_BIG_ENDIAN
 * include/PR/gbi.h` finds exactly one hit, at line 1743, and it guards a
 * pointer-width union, not these structs - so the native build gets the
 * big-endian field NAMES over little-endian field PLACES.
 *
 * MEASURED with the build's own compiler: starting from the word the
 * gsDPSetTileSize macro builds, `w0 = f2002002`,
 *
 *     .loadtile.sl = 90   ->  w0 = f2005a02   (lands in bits 8-19)
 *     .loadtile.tl = 100  ->  w0 = 06402002   (lands in bits 20-31)
 *
 * so `tl` overwrites the OPCODE BYTE, and the command's top byte becomes
 * tl >> 4.  Sixteen of the 256 values tl cycles through make that byte 0x06,
 * which ucode05.txt l.437-447 defines as rsp_uc05_displaylist - and the
 * settilesize's own lower word, 01000000 for tile 1, is then read as its
 * operand: segment 1, offset 0, the font list bound at lv.c:692.  That is
 * exactly what the Dam water marks show: MipMap2C_Something2_Setup (005ab520)
 * calling 01000000 from its +0x0018 in the frames where the water renders
 * untextured, and not calling it in the frames where it renders textured.
 *
 * The layout below is the authority's, not the struct's: ucode05.txt
 * "Display Lists and Object Generation/ucode05.txt" l.888-905, F2
 * rdp_settilesize, upper word 00FFF000 upper-left s / 00000FFF upper-left t,
 * opcode in the top byte via _SHIFTL(c, 24, 8).
 *
 * The cartridge path above is untouched and stays under #ifdef __sgi.
 */
/* B-109. The F2 field is an INTEGER quarter-texel; the phase the updater
 * computes below is CONTINUOUS. The truncation at this call is Rare's own
 * encoding limit - the cartridge could never show the fraction - but the
 * native renderer can, and dropping it is what makes the water hold a frozen
 * origin for several presented frames and then jump (measured at the owner's
 * Dam mark: S holds 3-4 presents, T 7-10, at the native delta=1 cadence the
 * hardware never reached). So this helper now takes the PHASE, writes the
 * SAME truncated word into the command - byte-for-byte what the u32 version
 * wrote, so the display list still carries exactly what the cartridge would
 * issue - and hands the full phase to the renderer as presentation-only
 * metadata, keyed by this command's own address. The renderer validates the
 * integer part against the command before using the fraction, so a stale or
 * foreign entry degrades to today's quantized origin, never to a wrong one.
 * sl_f2_frac_set is the same native-seam extern idiom frametiming.c uses for
 * sl_frame_advance; nothing here reaches the __sgi build. */
extern void sl_f2_frac_set(const void *cmd, f32 s_q, f32 t_q);

static void SL_SETTILESIZE_UL(Gfx *g, f32 uls, f32 ult)
{
    u32 iu = (u32) uls;
    u32 it = (u32) ult;
    g->words.w0 = (g->words.w0 & ~0x00FFFFFFu)
                | ((iu & 0xFFFu) << 12)
                |  (it & 0xFFFu);
    sl_f2_frac_set(g, uls, ult);
}

/* B-109. Tile 1's origin is the same phase offset by an integer, and the
 * cartridge expression `((s32)phase + off) & 0xFF` discards the fraction
 * before the helper ever sees it. This computes the identical integer with
 * the phase's own fraction re-attached: the truncation inside
 * SL_SETTILESIZE_UL then writes the exact byte the original expression
 * produced (off >= 0 and frac in [0,1), so truncating int+frac is int),
 * while the metadata keeps both tiles on the same continuous phase - the
 * (90,150) relative offset is preserved exactly. */
static f32 SL_WRAP_PHASE(f32 base, s32 off)
{
    return (f32) (((s32) base + off) & 0xFF) + (base - (f32) (s32) base);
}

#endif
// Water animation controller.
void sub_GAME_7F092E50(void)
{
#ifdef VERSION_EU
    f32 delta = g_GlobalTimerDelta;
#else
    f32 delta = g_ClockTimer;
#endif

    flt_CODE_bss_80079E80 += delta * 0.25f;

    if (flt_CODE_bss_80079E80 >= 256.0f)
    {
        flt_CODE_bss_80079E80 -= 256.0f;
    }

    if (flt_CODE_bss_80079E80 < 0.0f)
    {
        flt_CODE_bss_80079E80 += 256.0f;
    }

    flt_CODE_bss_80079E84 += delta * 0.1f;

    if (flt_CODE_bss_80079E84 >= 256.0f)
    {
        flt_CODE_bss_80079E84 -= 256.0f;
    }

    if (flt_CODE_bss_80079E84 < 0.0f)
    {
        flt_CODE_bss_80079E84 += 256.0f;
    }

    flt_CODE_bss_80079E88 += delta * 0.04f;

    // 6.2831802f is not quite equal to M_TAU_F. Leave as literal value here.
    if (flt_CODE_bss_80079E88 >= 6.2831802f)
    {
        flt_CODE_bss_80079E88 -= 6.2831802f;
    }

    if (flt_CODE_bss_80079E88 < 0.0f)
    {
        flt_CODE_bss_80079E88 += 6.2831802f;
    }
    
#ifdef __sgi
    MipMap2C_Something_Setup[2].loadtile.sl = flt_CODE_bss_80079E80;
    MipMap2C_Something_Setup[2].loadtile.tl = flt_CODE_bss_80079E84;
    MipMap2C_Something_Setup[3].loadtile.sl = ((s32)flt_CODE_bss_80079E80 + 90) & 0xFF;
    MipMap2C_Something_Setup[3].loadtile.tl = ((s32)flt_CODE_bss_80079E84 + 150) & 0xFF;
#else
    SL_SETTILESIZE_UL(&MipMap2C_Something_Setup[2],
                      flt_CODE_bss_80079E80,
                      flt_CODE_bss_80079E84);
    SL_SETTILESIZE_UL(&MipMap2C_Something_Setup[3],
                      SL_WRAP_PHASE(flt_CODE_bss_80079E80, 90),
                      SL_WRAP_PHASE(flt_CODE_bss_80079E84, 150));
#endif
    ((u32 *) MipMap2C_Something_Setup)[8] = (((u32 *) MipMap2C_Something_Setup)[8] & ~0xFF) | (u32) ((sinf(flt_CODE_bss_80079E88) * 127.0f) + 128.0f);

#ifdef __sgi
    MipMap2C_Something2_Setup[2].loadtile.sl = flt_CODE_bss_80079E80;
    MipMap2C_Something2_Setup[2].loadtile.tl = flt_CODE_bss_80079E84;
    MipMap2C_Something2_Setup[3].loadtile.sl = ((s32)flt_CODE_bss_80079E80 + 90) & 0xFF;
    MipMap2C_Something2_Setup[3].loadtile.tl = ((s32)flt_CODE_bss_80079E84 + 150) & 0xFF;
#else
    SL_SETTILESIZE_UL(&MipMap2C_Something2_Setup[2],
                      flt_CODE_bss_80079E80,
                      flt_CODE_bss_80079E84);
    SL_SETTILESIZE_UL(&MipMap2C_Something2_Setup[3],
                      SL_WRAP_PHASE(flt_CODE_bss_80079E80, 90),
                      SL_WRAP_PHASE(flt_CODE_bss_80079E84, 150));
#endif
    ((u32 *) MipMap2C_Something2_Setup)[8] = (((u32 *) MipMap2C_Something_Setup)[8] & ~0xFF) | (u32) ((sinf(flt_CODE_bss_80079E88) * 127.0f) + 128.0f);
}


Gfx* sub_GAME_7F09343C(Gfx *gdl, s32 arg1)
{
    if (arg1 != 0)
    {
        gSPDisplayList(gdl++, MipMap2C_Something_Setup);
    }
    else
    {
        gDPSetTile(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 4, 0, 0, 0, 0, 5, 0, 0, 5, 0);
        gDPSetTile(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 4, 0, 1, 0, 0, 5, 0, 0, 5, 0);
        gDPSetTileSize(gdl++, 0, 0, 0, 0, 0);
        gDPSetTileSize(gdl++, 1, 90, 150, 0, 0);
        gDPSetPrimColor(gdl++, 0, (sinf(flt_CODE_bss_80079E88) * 127.0f + 128.0f), 0xFF, 0xFF, 0xFF, 0xFF);
        gDPSetTextureDetail(gdl++, G_TD_CLAMP);
        gDPSetTextureFilter(gdl++, G_TF_BILERP);
        gDPSetCombineLERP(gdl++, TEXEL1, TEXEL0, PRIM_LOD_FRAC, TEXEL0, TEXEL1, TEXEL0, PRIM_LOD_FRAC, TEXEL0, COMBINED, 0, SHADE, 0, COMBINED, 0, SHADE, 0);
        gDPSetRenderMode(gdl++, G_RM_PASS, G_RM_AA_ZB_OPA_SURF2);
        gDPSetTextureLOD(gdl++, G_TL_TILE);
        gDPSetCycleType(gdl++, G_CYC_2CYCLE);
        gSPSetGeometryMode(gdl++, G_CULL_BACK);
    }
    return gdl;
}


Gfx* sub_GAME_7F09365C(Gfx *gdl, s32 arg1)
{
    if (arg1 != 0)
    {
        gSPDisplayList(gdl++, MipMap2C_Something2_Setup);
    }
    else
    {
        gDPSetTile(gdl++, G_IM_FMT_CI, G_IM_SIZ_8b, 2, 0, 0, 0, 0, 5, 0, 0, 5, 0);
        gDPSetTile(gdl++, G_IM_FMT_CI, G_IM_SIZ_8b, 2, 0, 1, 0, 0, 5, 0, 0, 5, 0);
        gDPSetTileSize(gdl++, 0, 0, 0, 0, 0);
        gDPSetTileSize(gdl++, 1, 90, 150, 0, 0);
        gDPSetPrimColor(gdl++, 0, (sinf(flt_CODE_bss_80079E88) * 127.0f + 128.0f), 0xFF, 0xFF, 0xFF, 0xFF);
        gDPSetTextureDetail(gdl++, G_TD_CLAMP);
        gDPSetTextureFilter(gdl++, G_TF_BILERP);
        gDPSetCombineLERP(gdl++, TEXEL1, TEXEL0, PRIM_LOD_FRAC, TEXEL0, TEXEL1, TEXEL0, PRIM_LOD_FRAC, TEXEL0, COMBINED, 0, SHADE, 0, COMBINED, 0, SHADE, 0); /* expands to FC272C04 1F1093FF */
        gDPSetRenderMode(gdl++, G_RM_PASS, G_RM_AA_ZB_OPA_SURF2);
        gDPSetTextureLOD(gdl++, G_TL_TILE);
        gDPSetCycleType(gdl++, G_CYC_2CYCLE);
        gSPSetGeometryMode(gdl++, G_CULL_BACK);
    }

    return gdl;
}
