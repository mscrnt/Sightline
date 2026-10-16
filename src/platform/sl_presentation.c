/**
 * sl_presentation.c - the PRESENTATION POLICY (#68). See sl_presentation.h for
 * the contract, the mapping and why this layer exists at all.
 *
 * WHY src/platform. Same reason as the settings store beside it: this is pure
 * host code over plain ints, and it reads and writes the store. The two
 * providers it resolves for live in src/gfx and include it the way they
 * already include sl_settings.h; the game-facing half - the one HUD line the
 * cycle action shows - is src/native/sl_settings_apply.c, which needs game
 * headers and only ever calls the functions below.
 *
 * NO FRAMEWORK, and deliberately no registration mechanism. ONE table, one row
 * per presentation, one column per provider class. A new provider class is a
 * new column and a new resolver; it is not a plugin system, because three rows
 * do not need one.
 *
 * SELF-TEST: tools/windows/presentationtest.ps1 (compile this file with
 * -DSL_PRESENTATION_SELFTEST beside sl_settings.c and run it). Every fixture is
 * synthesised in code and the store is left INACTIVE, so nothing is written.
 */
#include "sl_presentation.h"
#include "sl_settings.h"

#include <stdlib.h>
#include <string.h>

/* A presentation fog row's own developer switch: set to 0, the row is set
 * aside and the authored value stands. NULL for the rows whose only switches
 * are the renderer's table-wide ones (SL_XBLA_AUTHENTIC_FOG, SL_XBLA_FOG_START). */
static int fog_row_on(const char *off)
{
    const char *s;
    if (off == (const char *) 0) return 1;
    s = getenv(off);
    return s == (const char *) 0 || *s != '0';
}

/* THE MAPPING. Index == the presentation id. The texture column names a
 * SL_TEXTURES_* set; the world column names the world-asset provider's own set
 * ("" = the provider dark). They happen to line up today because the three
 * bundles were assembled from the same three sources - that is a fact about
 * the content, not an identity, which is why it is written out rather than
 * assumed. A presentation whose texture set differed from its name would need
 * nothing here but a different entry. */
struct sl_presentation_row {
    const char *name;
    int         textures;
    const char *world_set;
};

static const struct sl_presentation_row s_rows[SL_PRESENTATION_COUNT] = {
    /* ORIGINAL     */ { "ORIGINAL",     SL_TEXTURES_ORIGINAL,  "" },
    /* COMMUNITY HD */ { "COMMUNITY HD", SL_TEXTURES_COMMUNITY, "" },
    /* XBLA         */ { "XBLA",         SL_TEXTURES_XBLA,      "xbla" }
};

static int presentation_ok(int p)
{
    return p >= 0 && p < SL_PRESENTATION_COUNT;
}

const char *sl_presentation_name(int presentation)
{
    return presentation_ok(presentation) ? s_rows[presentation].name : "?";
}

/* ---- the canonical value ------------------------------------------------- */

int sl_presentation(void)
{
    int v = sl_settings_get(SL_SET_PRESENTATION);
    return presentation_ok(v) ? v : SL_PRESENTATION_ORIGINAL;
}

void sl_presentation_set(int presentation)
{
    if (!presentation_ok(presentation)) return;
    sl_settings_set(SL_SET_PRESENTATION, presentation);
}

void sl_presentation_step(int dir)
{
    int v = sl_presentation() + (dir < 0 ? -1 : 1);
    if (v < 0) v = SL_PRESENTATION_COUNT - 1;
    if (v >= SL_PRESENTATION_COUNT) v = 0;
    sl_presentation_set(v);
}

/* ---- the resolution ------------------------------------------------------ */

int sl_presentation_textures(int presentation)
{
    if (!presentation_ok(presentation)) presentation = SL_PRESENTATION_ORIGINAL;
    return s_rows[presentation].textures;
}

const char *sl_presentation_world_set(int presentation)
{
    if (!presentation_ok(presentation)) presentation = SL_PRESENTATION_ORIGINAL;
    return s_rows[presentation].world_set;
}

/* ---- presentation fog range (D-025) ---------------------------------------
 *
 * One row per adjusted environment. The authored range is not repeated here:
 * it stays in the set's own metadata, and the provider keeps it beside the
 * value this table presents. */
struct sl_presentation_fog_row {
    const char *world_set;
    unsigned    env_id;
    float       fog_end;         /* world units, the same unit as SLEN's */
    const char *off;             /* the row's own switch (fog_row_on), or NULL */
};

static const struct sl_presentation_fog_row s_fog_range[] = {
    /* Dam gameplay (authored end 15000). The end is placed on the cartridge's
     * own Dam fog curve, which rises as 1.0667 - 5001.7 / d (world units;
     * the level band 995..1000 per mille under near 5, far 15000, visibility
     * 0.2) and never reaches full fog before the far plane (75000). 34074 is
     * where the linear range matches that curve at the farthest mountain
     * (30793 units, 90.4% fog); 75000 is the curve's own full-fog distance,
     * and at 75000 the panorama shell's fade (D-026) no longer lands on
     * fogged scenery. This row takes their midpoint: the range behind the
     * walkway, 20000..35000 units out, keeps its silhouettes and snow a third
     * to two thirds out of the fog colour, and the outer ring still fades to
     * haze inside the far plane. */
    { "xbla", 33u, 54537.0f, (const char *) 0 },
    /* Surface gameplay (authored end 45000), D-029: SHORTENED. The end is the
     * distance at which the game stops drawing a prop or a character - the
     * far-fog cut in fogGetPropDistColor (bgfog.c), where the cartridge's own
     * object fog reaches 1: zDepth 12071.2 world units on this row
     * (BlendMultiplier 2 and FarFog 2500 at the stage's visibility scale 0.2,
     * so 10 and 12500, with the intensities 996 / 1000). That cut tests the
     * view depth and this fog the straight-line distance, which is never
     * shorter, so a prop is wholly fog-coloured by the time the game removes
     * it: the satellite dish comes out of the fog instead of appearing whole
     * at 12071 units. */
    { "xbla", 36u, 12071.0f, (const char *) 0 },
    /* Surface 2 gameplay (authored end 10000), D-029 on the second Surface:
     * the same rule, the same cut. BlendMultiplier 2 and FarFog 2000 at the
     * visibility scale 0.2 (10 and 10000) with the intensities 957 / 1000 put
     * the far-fog prop cut at zDepth 7662.5 world units (measured,
     * SL_TABLE_DBG's cutoff-zdepth). Under the authored 10000 the satellite
     * dish and the other props drawn past 7662 were removed while the
     * replacement around them was still 23% clear, so they appeared whole at
     * the cut instead of out of the fog. */
    { "xbla", 43u, 7662.0f, (const char *) 0 },
    /* Egyptian's second environment (authored -100 .. 10000, black), D-055:
     * SHORTENED with its start (s_fog_start). The game blends Egyptian's row
     * toward this one when Baron Samedi's second death runs ai_24's
     * gas_leak_and_fade_fog (fogSwitchToSolosky2, 120 ticks), and its own fog
     * on this row is a depth band 972..1000 per mille under near 10 and far
     * 10000 at visibility 1.0: 1.0358 - 357.5 / d of the fog colour, from 345
     * world units, 68% at 1000, 86% at 2000. The authored linear row is 11%
     * at 1000, so the remaster's rooms barely darkened where the cartridge's
     * close into black (mean frame luma over four poses - rooms 31, 3, 12 and
     * 19 - four headings each, the fade forced at a fixed poll: ORIGINAL
     * -30.5% across the fade, XBLA as authored -6.2%). The end is a design
     * value that lands those poses on ORIGINAL's darkening: trials at 1500,
     * 1600 and 2600 gave -30.5%, -28.3% and -15.7%, so 1550 (-29.4%). The
     * colour stays authored (black, the game's own). SL_XBLA_FADE_FOG=0 sets
     * the row aside. */
    { "xbla", 132u, 1550.0f, "SL_XBLA_FADE_FOG" }
};

float sl_presentation_fog_end(const char *world_set, unsigned env_id,
                              float authored_end)
{
    unsigned i;
    if (world_set == (const char *) 0) return authored_end;
    for (i = 0; i < sizeof s_fog_range / sizeof s_fog_range[0]; i++)
        if (s_fog_range[i].env_id == env_id
            && strcmp(s_fog_range[i].world_set, world_set) == 0
            && fog_row_on(s_fog_range[i].off))
            return s_fog_range[i].fog_end;
    return authored_end;
}

/* ---- presentation fog start (D-043) ---------------------------------------
 *
 * Statue gameplay (environment 22; authored start -100, end 3500, colour
 * 0 0 8, linear in distance). The game's own fog on this row is a far band
 * (bgfog.c fog_tables, LEVELID_STATUE: NearFog 2000, FarFog 3500, intensity
 * 996..1000 per mille): ORIGINAL frames with the game's fog switched off
 * (SL_FOG=0) are within 7% of the fogged ones at 16 Statue poses (mean luma
 * 25.4 against 23.9), so the near and middle park is unfogged. The remaster's
 * row fogs from the eye: 30% at 1000 units, 58% at 2000, toward a colour
 * that is almost black. Measured over 31 Statue poses (mean frame luma, read
 * 80): ORIGINAL 22.95, XBLA as authored 19.23, XBLA with the stage fog off
 * 24.90 - the fog is the larger part of the difference (the remaster's dark
 * baked vertex colour, area mean 61 of 255, is the rest). The start is moved
 * out to 800 units, a design value that lands the same 31 poses between the
 * two presentations (22.64, 17.7% over the authored row; trials at 700,
 * 1100 and 1500 gave 22.44, 23.10 and 23.57): the near ground and walls
 * read, and the park still
 * closes into the same night. The end and colour stay authored, so a prop at
 * the game's far-fog cut (zDepth 3476.3) is still 99% fogged and comes out of
 * the fog with the world as before. */
struct sl_presentation_fog_start_row {
    const char *world_set;
    unsigned    env_id;
    float       fog_start;       /* world units, the same unit as SLEN's */
    const char *off;             /* the row's own switch (fog_row_on), or NULL */
};

static const struct sl_presentation_fog_start_row s_fog_start[] = {
    { "xbla", 22u, 800.0f, (const char *) 0 },
    /* Egyptian's second environment, D-055 (see s_fog_range): the start is
     * where the game's own fog on that row begins - 10 / (1 - 0.972 x 9990 /
     * 10000) = 345 world units - so the room within it stays clear, as the
     * cartridge's does. */
    { "xbla", 132u, 345.0f, "SL_XBLA_FADE_FOG" }
};

float sl_presentation_fog_start(const char *world_set, unsigned env_id,
                                float authored_start)
{
    unsigned i;
    if (world_set == (const char *) 0) return authored_start;
    for (i = 0; i < sizeof s_fog_start / sizeof s_fog_start[0]; i++)
        if (s_fog_start[i].env_id == env_id
            && strcmp(s_fog_start[i].world_set, world_set) == 0
            && fog_row_on(s_fog_start[i].off))
            return s_fog_start[i].fog_start;
    return authored_start;
}

/* ---- the game's atmosphere (D-030) ----------------------------------------
 *
 * Surface gameplay. The remaster's row (an extension of fog_tables' row 36 in
 * its executable, read by tools/worldasset/xexenv.py) fogs toward 164 150 130,
 * and every other row of that table it carries keeps the decomp's colour
 * except the three stages it re-dressed - the Dam and Runway (both 133 173
 * 202, a clear-sky blue) and this one; its sky dome for Surface is byte for
 * byte the Dam's, the one image all eleven of the source's domes carry. Both
 * are an unfinished beta's: a warm haze under a clear sky. The game's row is
 * 96 96 128 with a cloud layer coloured 240 120 30 over it, which sky.c fades
 * into the fog colour toward the horizon - the cold storm the stage is.
 *
 * Surface 2, both of its rows: gameplay (43) and the intro cinema (943). The
 * remaster's rows keep the decomp's colour (32 16 16) there, but the dome the
 * source carries for the stage (`skydome/surface2`) is the same daylight image
 * as every other source dome - the same two texture ids as Surface's - drawn
 * over a stage the game plays at night: fog 32 16 16 under a cloud layer
 * coloured 58 17 0. Shaded as the game's cloud layer, the dome reads as that
 * night.
 *
 * Streets gameplay (29), D-044. The remaster's row keeps the decomp's colour
 * (16 24 32, a dusk navy) and the source carries no dome for the stage; the
 * dome lent to it (sl_presentation_sky_lent) is shaded with the row's own
 * fog colour and its cloud layer's colour, 225 175 100. The fog colour this
 * also puts on the row is the colour the row already holds.
 *
 * Depot gameplay (30), D-044 extended (#92). The same case: the source holds
 * no dome for the stage, and its row keeps the decomp's colour (0 0 8,
 * starting at -100 and ending at 5000); the lent dome is shaded with that
 * fog colour and the row's cloud colour, 70 199 186 - the game's teal cloud
 * layer over a near-black night.
 *
 * Cradle gameplay (41), D-044 extended (#96). The same case again: the
 * source holds `background/cradle` and no dome, and the remaster's row keeps
 * the decomp's colour (96 128 160) from -100 to 30000; the lent dome is
 * shaded with that fog colour and the row's cloud colour, 255 255 0. The
 * stage has no cinema row, so its intro and ending play under this one. */
static const struct { const char *world_set; unsigned env_id; } s_game_atmosphere[] = {
    { "xbla", 29u },
    { "xbla", 30u },
    { "xbla", 36u },
    { "xbla", 41u },
    { "xbla", 43u },
    { "xbla", 943u }
};

int sl_presentation_game_atmosphere(const char *world_set, unsigned env_id)
{
    unsigned i;
    if (world_set == (const char *) 0) return 0;
    for (i = 0; i < sizeof s_game_atmosphere / sizeof s_game_atmosphere[0]; i++)
        if (s_game_atmosphere[i].env_id == env_id
            && strcmp(s_game_atmosphere[i].world_set, world_set) == 0)
            return 1;
    return 0;
}

/* ---- props drawn past the game's far-fog cut (D-041) ----------------------
 *
 * Surface 2 gameplay. The game removes a prop once its object fog passes 1
 * (fogGetPropDistColor, bgfog.c): zDepth 7662.5 world units on this row, a
 * test of the prop's ORIGIN. The presented fog follows each vertex's
 * straight-line distance instead, and the satellite dish (`sevdish`, an
 * autogun prop) is large: measured from the same frames under two fog ends,
 * the faces it shows on crossing the cut lie 6943..7370 units out (median
 * 7127), only 93% fogged, beside a background (the dish's own building, rooms
 * 31 / 32) drawn at 6900..7100 and 90% fogged. So the dish appeared at the
 * cut, about as visible as the building it stands on had already been. The
 * remaster fogs props and characters with the row the background takes
 * (sl_wa_object_fog_env), so drawing a prop past the cut fogs it exactly as
 * the world around it: it comes out of the fog with the building. */
static const struct { const char *world_set; unsigned env_id; } s_prop_fog_drawn[] = {
    { "xbla", 43u }
};

int sl_presentation_prop_fog_drawn(const char *world_set, unsigned env_id)
{
    unsigned i;
    if (world_set == (const char *) 0) return 0;
    for (i = 0; i < sizeof s_prop_fog_drawn / sizeof s_prop_fog_drawn[0]; i++)
        if (s_prop_fog_drawn[i].env_id == env_id
            && strcmp(s_prop_fog_drawn[i].world_set, world_set) == 0)
            return 1;
    return 0;
}

/* ---- a borrowed sky (D-031) ------------------------------------------------
 *
 * Bunker 1 (stage 9, the source's item `bunker`). The game draws no sky
 * there: the stage has no environment row in fog_tables, so bgfog.c loads
 * the fogless record and sky.c fills the view with its navy (0 16 64) - the
 * remaster keeps that fill behind the start shaft's grate. The stage sits
 * under Surface 1, and its one view of the outside is up that shaft. Under
 * the xbla set it takes Surface's sky instead: the same skydome item
 * (`surface`), drawn under environment row 36's atmosphere - its fog and
 * cloud colours and its cloud layer, D-030's and D-023's mechanism - so the
 * sky up the shaft reads as the hour Surface is played at. A deliberate
 * continuity change, not a remaster fact. The sky only: the stage keeps its
 * own fogless range and fill everywhere else. */
static const struct {
    const char *world_set;
    const char *item;        /* the stage's item path (sl_wa_stage_item_path) */
    const char *sky_item;    /* the skydome item drawn in its place */
    unsigned    env_id;      /* the environment row whose atmosphere shades it */
} s_sky_borrow[] = {
    { "xbla", "bunker", "surface", 36u }
};

const char *sl_presentation_sky_borrow(const char *world_set, const char *item,
                                       unsigned *env_id)
{
    unsigned i;
    if (world_set == (const char *) 0 || item == (const char *) 0) return (const char *) 0;
    for (i = 0; i < sizeof s_sky_borrow / sizeof s_sky_borrow[0]; i++)
        if (strcmp(s_sky_borrow[i].item, item) == 0
            && strcmp(s_sky_borrow[i].world_set, world_set) == 0) {
            if (env_id != (unsigned *) 0) *env_id = s_sky_borrow[i].env_id;
            return s_sky_borrow[i].sky_item;
        }
    return (const char *) 0;
}

/* ---- a lent dome (D-044) ---------------------------------------------------
 *
 * Streets (stage 29, the source's item `streets`). The source carries a
 * background for the stage and no skydome, and the remaster draws a plain
 * navy over it (private emulator, round str1); the game draws its own cloud
 * layer there - environment row 29: fog 16 24 32, clouds 225 175 100. Every
 * other outdoor stage of the xbla set draws a source dome under the game's
 * atmosphere (D-030, D-034, D-040), so Streets takes one too: the dome the
 * renderer draws in place of the absent `skydome/streets`, shaded by the
 * stage's OWN row, read live (s_game_atmosphere names row 29).
 *
 * The item lent is `frigate`. Every source dome carries the same daylight
 * image, so the choice is between preparations, and Frigate's is the one
 * written as cloud cover (D-034): a texel of blue sky is no cloud and draws
 * the fog colour, a texel of cloud the cloud shade. Under a bright cloud
 * colour the combiner reads every bright texel of an image prepared as
 * authored as cloud, so the whole clear sky would draw lit (measured on
 * Frigate's mark 1 before D-034, and on Streets in round str2). The cap
 * (D-023) comes with it. Nothing else of Frigate reaches the stage.
 *
 * Depot (stage 30, the source's item `depot`), #92: the same absence - the
 * source carries `background/depot` and no dome - and the remaster draws the
 * sky black (private emulator, round dep1). The same dome, shaded by Depot's
 * own row 30 (s_game_atmosphere), meeting the fog where the game's cloud
 * layer ends (D-046), as on Streets.
 *
 * Cradle (stage 41, the source's item `cradle`), #96: the same absence - the
 * source carries `background/cradle` and no dome (its eleven domes are
 * bunker, bunker2, caverns, complex, dam, egyptian, frigate, runway,
 * surface, surface2 and temple) - over the game's cloud layer, row 41. The
 * same dome, shaded by row 41 (s_game_atmosphere), with D-046's edge. */
static const struct {
    const char *world_set;
    const char *item;        /* the stage's item path (sl_wa_stage_item_path) */
    const char *sky_item;    /* the skydome item drawn for it */
} s_sky_lent[] = {
    { "xbla", "streets", "frigate" },
    { "xbla", "depot",   "frigate" },
    { "xbla", "cradle",  "frigate" }
};

const char *sl_presentation_sky_lent(const char *world_set, const char *item)
{
    unsigned i;
    if (world_set == (const char *) 0 || item == (const char *) 0) return (const char *) 0;
    for (i = 0; i < sizeof s_sky_lent / sizeof s_sky_lent[0]; i++)
        if (strcmp(s_sky_lent[i].item, item) == 0
            && strcmp(s_sky_lent[i].world_set, world_set) == 0)
            return s_sky_lent[i].sky_item;
    return (const char *) 0;
}

/* ---- the game's atmosphere on a fogless stage (D-034) ----------------------
 *
 * Frigate (stage 26, the source's item `frigate`). The game loads the fogless
 * record there (fog_tables2, bgfog.c): no fog, sky navy 16 48 96, a cloud
 * layer 230 230 230 at height 3000 and the sea (sky.c) below it, so
 * sl_fog_environment_id answers -1 and D-030's row-keyed table cannot name
 * it. The remaster's dome for the stage is the clear daylight image every
 * source dome carries, with a band of snowy mountains at its foot, drawn over
 * the game's own night-navy sea - the remaster keeps that sea (its water
 * images are the game's 32x32 tiles). Under the xbla set the dome is shaded as
 * the game shades its cloud layer, with the record's colours, so sky, horizon
 * and sea are one hour again. */
static const struct { const char *world_set; const char *item; } s_fogless_atmosphere[] = {
    { "xbla", "frigate" }
};

int sl_presentation_fogless_atmosphere(const char *world_set, const char *item)
{
    unsigned i;
    if (world_set == (const char *) 0 || item == (const char *) 0) return 0;
    for (i = 0; i < sizeof s_fogless_atmosphere / sizeof s_fogless_atmosphere[0]; i++)
        if (strcmp(s_fogless_atmosphere[i].item, item) == 0
            && strcmp(s_fogless_atmosphere[i].world_set, world_set) == 0)
            return 1;
    return 0;
}

/* ---- a prop item the remaster authored as another object (D-038) ---------
 *
 * prop/chrplastique is, in both of the source's generations, a round mounted
 * device (-421 .. 536 x -205 .. 235 x -208 .. 208 item units) where the game's
 * PchrplastiqueZ is a brick (-120 .. 120 x 0 .. 104 x -60 .. 60); the box test
 * refuses it (axis 2 off by 12%). The game places it on Frigate's consoles. */
static const struct { const char *world_set; const char *item; } s_prop_fit[] = {
    { "xbla", "chrplastique" }
};

int sl_presentation_prop_fit(const char *world_set, const char *item)
{
    unsigned i;
    if (world_set == (const char *) 0 || item == (const char *) 0) return 0;
    for (i = 0; i < sizeof s_prop_fit / sizeof s_prop_fit[0]; i++)
        if (strcmp(s_prop_fit[i].item, item) == 0
            && strcmp(s_prop_fit[i].world_set, world_set) == 0)
            return 1;
    return 0;
}

/* ---- a one-piece item over a model that moves parts (D-045) ---------------
 *
 * prop/jeep is, in both of the source's generations, a STATIC item: no
 * skeleton, its four wheels in the body's own draws (one 3602-triangle strip
 * holds the body, the arches and the wheels), so the remaster draws it as one
 * rigid piece. The game's PjeepZ moves five parts (the `car` skeleton: base,
 * wheel1..wheel4), and the prop seam refuses a one-piece item on such a
 * model, so the jeep stayed the game's flat-green model with its black
 * windscreen under XBLA (playtest run 20261008-214033-lvl29, mark 1; the
 * runtime line `prop item on PjeepZ REFUSED - the model moves 5 parts`).
 * The only refusal of that kind in any recorded run. Named here, the item
 * rides the model's root matrix (the base) whole: placed, turned and moved
 * with the vehicle as the game moves it, its wheels in the rest pose - they
 * do not turn or steer, as the remaster's item cannot. Streets' jeeps stand
 * parked; the one the stage drives keeps the rest pose too. */
static const struct { const char *world_set; const char *item; } s_prop_rigid[] = {
    { "xbla", "jeep" }
};

int sl_presentation_prop_rigid(const char *world_set, const char *item)
{
    unsigned i;
    if (world_set == (const char *) 0 || item == (const char *) 0) return 0;
    for (i = 0; i < sizeof s_prop_rigid / sizeof s_prop_rigid[0]; i++)
        if (strcmp(s_prop_rigid[i].item, item) == 0
            && strcmp(s_prop_rigid[i].world_set, world_set) == 0)
            return 1;
    return 0;
}

/* ---- a room the remaster remodelled whole takes no native fill ----------
 *
 * Frigate's room 57 is the boarding boat, which the remaster remodels as an
 * object of its own (another hull, gunwale and bow). The #78 native fill
 * judges room triangles against the replacement surface by surface: 17 of
 * the room's 64 triangles register on the remodelled boat and 14 of them are
 * holed (the gunwale strip 3.38 item units from it at its farthest point,
 * against the 2.5 world-unit hole radius, 3.04 item units; hull panels at
 * x 39 .. 47), the sea is open behind them, so they drew 12 units behind
 * their plane and showed past the remaster's silhouette as white shards (vertex colour
 * fefefe; playtest run 20261006-201517, marks 1 and 2). Runway's bunker-door
 * strip, which the fill exists for, is the same class by every measure the
 * fill has (room 4: 13 of 13 registered triangles holed, open behind), so
 * the room is named here rather than the rule changed. */
static const struct { const char *world_set; const char *item; int room; } s_fill_withheld[] = {
    { "xbla", "frigate", 57 }
};

int sl_presentation_fill_withheld(const char *world_set, const char *item, int room)
{
    unsigned i;
    if (world_set == (const char *) 0 || item == (const char *) 0) return 0;
    for (i = 0; i < sizeof s_fill_withheld / sizeof s_fill_withheld[0]; i++)
        if (s_fill_withheld[i].room == room && strcmp(s_fill_withheld[i].item, item) == 0
            && strcmp(s_fill_withheld[i].world_set, world_set) == 0)
            return 1;
    return 0;
}

/* ---- the game's translucent detail the replacement left out (#96) --------
 *
 * Cradle's room 7 is the shaft from the cradle down to the bottom platform:
 * a pole and, beside it, the game's ladder - one translucent quad (room 7's
 * secondary list, texture 844, 2 triangles at world z 76, x -68 .. -17, y
 * -2456 .. -1205). The remaster's item carries the pole (draw 0xe44) and no
 * ladder in either generation, and the remaster draws none (private
 * emulator, round crd6); so with the room suppressed the shaft had no
 * ladder (playtest run 20261012-142635, mark 1). sl_gfx_dl.c nf_room_kept
 * draws a translucent triangle of a room named here where no replacement
 * surface lies within the fill's hole radius of its corners and centroid.
 * Measured from the ROM's room lists, Cradle's other translucent triangles
 * that far from the replacement are the arms' overhead cables (texture 213,
 * rooms 34 .. 36) and two rail runs (texture 1437, rooms 10 and 14), which
 * the remaster models itself - so the room is named rather than the rule
 * made general.
 *
 * ONE ENTRY, of the xbla set: Cradle (`cradle`) room 7. */
static const struct { const char *world_set; const char *item; int room; } s_native_kept[] = {
    { "xbla", "cradle", 7 }
};

int sl_presentation_native_kept(const char *world_set, const char *item, int room)
{
    unsigned i;
    if (world_set == (const char *) 0 || item == (const char *) 0) return 0;
    for (i = 0; i < sizeof s_native_kept / sizeof s_native_kept[0]; i++)
        if (s_native_kept[i].room == room && strcmp(s_native_kept[i].item, item) == 0
            && strcmp(s_native_kept[i].world_set, world_set) == 0)
            return 1;
    return 0;
}

/* ---- the game's fade where the replacement has nothing (D-035) ----------
 *
 * The #83 rule draws a translucent room triangle whose shade spans 32 or
 * darker to 224 or brighter where no replacement surface lies within the
 * fill's registration radius (sl_gfx_dl.c nf_fade_kept). D-035 brought it in
 * for one stage, Frigate, whose remaster hull is cut at the game's waterline
 * so that the game's skirt fades the hull into the sea; its census then
 * covered the six stages accepted before it (none carry a fade). The shade
 * test alone does not single out a waterline: measured from the ROM's room
 * lists, Jungle carries 96 such triangles (87 with no replacement surface
 * near) - the bamboo backdrop cards round its clearings, lit dark green at
 * the foot and white at the top - and drawn under XBLA they showed as flat
 * panels of the game's low-resolution backdrop through gaps in the
 * remaster's canopy (Jungle playtest mark 2, 2026-10-10; with the fade off
 * the remaster's own canopy closes the same view). Train carries 42 (22
 * with no surface near: the game's track strips at its rail ends, which the
 * remaster models itself; one drew as a white line floating over the
 * remaster's rails) and Statue one. So the fade is drawn on the stages
 * named here, where a preparation divergence withholds the remaster's own
 * surface for it.
 *
 * ONE ENTRY, of the xbla set: Frigate (`frigate`). */
static const struct { const char *world_set; const char *item; } s_game_fade[] = {
    { "xbla", "frigate" }
};

int sl_presentation_game_fade(const char *world_set, const char *item)
{
    unsigned i;
    if (world_set == (const char *) 0 || item == (const char *) 0) return 0;
    for (i = 0; i < sizeof s_game_fade / sizeof s_game_fade[0]; i++)
        if (strcmp(s_game_fade[i].item, item) == 0 && strcmp(s_game_fade[i].world_set, world_set) == 0)
            return 1;
    return 0;
}

/* ---- Sightline's own sea (D-039) ----------------------------------------
 *
 * Frigate (stage 26, the source's item `frigate`). The game draws its sea
 * from sky.c: the 32x32 water image under the shade sky.c gives each corner,
 * bright near the boat and the sky's navy at the horizon - but the image
 * multiplies that navy, so the sea meets the sky a band darker than the sky
 * (measured at the playtest's boat pose: sea 2 14 49 against sky 17 49 97 at
 * the horizon). The remaster draws one constant navy. Under the xbla set the
 * sea is drawn by Sightline's own program (sl_gfx_dl.c, the sea program): the
 * game's image and shade, in layers that break its repeat, the sky's
 * reflection by the angle of view and the sky's horizon colour at the
 * horizon. */
static const struct { const char *world_set; const char *item; } s_own_sea[] = {
    { "xbla", "frigate" }
};

int sl_presentation_own_sea(const char *world_set, const char *item)
{
    unsigned i;
    if (world_set == (const char *) 0 || item == (const char *) 0) return 0;
    for (i = 0; i < sizeof s_own_sea / sizeof s_own_sea[0]; i++)
        if (strcmp(s_own_sea[i].item, item) == 0
            && strcmp(s_own_sea[i].world_set, world_set) == 0)
            return 1;
    return 0;
}

/* ============================================================ self-test == */
#ifdef SL_PRESENTATION_SELFTEST
#include <stdio.h>
#include <string.h>

static int g_pass, g_fail;
static void ck(int cond, const char *what)
{
    if (cond) { g_pass++; }
    else      { g_fail++; printf("FAIL: %s\n", what); }
}

int main(void)
{
    int p;

    printf("sl_presentation self-test (#68: the presentation policy)\n\n");

    /* 1. THE THREE NAMES, and the refusal outside the range. */
    ck(strcmp(sl_presentation_name(SL_PRESENTATION_ORIGINAL), "ORIGINAL") == 0,
       "ORIGINAL is named ORIGINAL");
    ck(strcmp(sl_presentation_name(SL_PRESENTATION_COMMUNITY), "COMMUNITY HD") == 0,
       "COMMUNITY is named COMMUNITY HD");
    ck(strcmp(sl_presentation_name(SL_PRESENTATION_XBLA), "XBLA") == 0,
       "XBLA is named XBLA");
    ck(strcmp(sl_presentation_name(SL_PRESENTATION_COUNT), "?") == 0
       && strcmp(sl_presentation_name(-1), "?") == 0,
       "an out-of-range presentation has no name");

    /* 2. THE THREE MAPPINGS - the whole of the bundle decision, one
     *    check per provider per presentation. */
    ck(sl_presentation_textures(SL_PRESENTATION_ORIGINAL) == SL_TEXTURES_ORIGINAL,
       "ORIGINAL resolves textures ORIGINAL");
    ck(sl_presentation_world_set(SL_PRESENTATION_ORIGINAL)[0] == '\0',
       "ORIGINAL resolves NO world assets");
    ck(sl_presentation_textures(SL_PRESENTATION_COMMUNITY) == SL_TEXTURES_COMMUNITY,
       "COMMUNITY resolves textures COMMUNITY");
    ck(sl_presentation_world_set(SL_PRESENTATION_COMMUNITY)[0] == '\0',
       "COMMUNITY resolves NO world assets - the community pack is a texture pack");
    ck(sl_presentation_textures(SL_PRESENTATION_XBLA) == SL_TEXTURES_XBLA,
       "XBLA resolves textures XBLA");
    ck(strcmp(sl_presentation_world_set(SL_PRESENTATION_XBLA), "xbla") == 0,
       "XBLA resolves the xbla world-asset set - the Dam skydome follows");

    /* 3. NO MAPPING CROSSES A SET. Whatever a presentation selects, it is
     *    that presentation's own set or nothing: no row names another row's
     *    texture set, which is the per-provider fallback contract stated as a
     *    property of the table rather than of the providers. */
    for (p = 0; p < SL_PRESENTATION_COUNT; p++) {
        int t = sl_presentation_textures(p);
        ck(t == p || t == SL_TEXTURES_ORIGINAL,
           "a presentation selects its own texture set or ORIGINAL, never another's");
    }
    ck(strcmp(sl_presentation_world_set(SL_PRESENTATION_XBLA), "community") != 0,
       "no presentation selects a world set belonging to another presentation");

    /* 4. AN INVALID VALUE RESOLVES AS ORIGINAL - the same refusal the store,
     *    the providers and the UI all make. */
    ck(sl_presentation_textures(-1) == SL_TEXTURES_ORIGINAL
       && sl_presentation_textures(SL_PRESENTATION_COUNT) == SL_TEXTURES_ORIGINAL
       && sl_presentation_textures(99) == SL_TEXTURES_ORIGINAL,
       "an out-of-range presentation resolves textures ORIGINAL");
    ck(sl_presentation_world_set(-1)[0] == '\0'
       && sl_presentation_world_set(SL_PRESENTATION_COUNT)[0] == '\0'
       && sl_presentation_world_set(99)[0] == '\0',
       "an out-of-range presentation resolves NO world assets");

    /* 5. THE CANONICAL VALUE. The store is INACTIVE here (no init), so sets
     *    change memory only and nothing is written - which is exactly the
     *    state a synthetic test wants. */
    ck(sl_presentation() == SL_PRESENTATION_ORIGINAL,
       "the default presentation is ORIGINAL");

    sl_presentation_set(SL_PRESENTATION_XBLA);
    ck(sl_presentation() == SL_PRESENTATION_XBLA, "set XBLA reads back XBLA");
    ck(sl_settings_get(SL_SET_PRESENTATION) == SL_PRESENTATION_XBLA,
       "the value the policy holds IS the store's presentation row - no copy");

    sl_presentation_set(SL_PRESENTATION_COUNT);
    ck(sl_presentation() == SL_PRESENTATION_XBLA,
       "an out-of-range set is refused and leaves the value alone");
    sl_presentation_set(-1);
    ck(sl_presentation() == SL_PRESENTATION_XBLA, "a negative set is refused too");

    /* 6. F5 CYCLES EXACTLY 0 -> 1 -> 2 -> 0. The whole of the action's
     *    semantics, and the property that three presses are a no-op. */
    sl_presentation_set(SL_PRESENTATION_ORIGINAL);
    sl_presentation_step(1);
    ck(sl_presentation() == SL_PRESENTATION_COMMUNITY, "step 1: ORIGINAL -> COMMUNITY");
    sl_presentation_step(1);
    ck(sl_presentation() == SL_PRESENTATION_XBLA, "step 2: COMMUNITY -> XBLA");
    sl_presentation_step(1);
    ck(sl_presentation() == SL_PRESENTATION_ORIGINAL, "step 3: XBLA wraps to ORIGINAL");
    {
        int i, start = sl_presentation();
        for (i = 0; i < SL_PRESENTATION_COUNT; i++) sl_presentation_step(1);
        ck(sl_presentation() == start, "one full cycle returns where it started");
    }

    /* Backwards, for the editors' LEFT. */
    sl_presentation_set(SL_PRESENTATION_ORIGINAL);
    sl_presentation_step(-1);
    ck(sl_presentation() == SL_PRESENTATION_XBLA, "a backward step off ORIGINAL wraps to XBLA");
    sl_presentation_step(-1);
    ck(sl_presentation() == SL_PRESENTATION_COMMUNITY, "backward: XBLA -> COMMUNITY");

    /* 7. ONE VALUE, EVERY SURFACE. The two editors step, the cycle action
     *    steps, and each of the three writes the SAME row the providers read -
     *    so the resolution follows immediately and without a reload. The
     *    surfaces themselves are UI; what is provable here is that there is
     *    only one place for them to write. */
    sl_presentation_set(SL_PRESENTATION_ORIGINAL);
    ck(sl_presentation_textures(sl_presentation()) == SL_TEXTURES_ORIGINAL
       && sl_presentation_world_set(sl_presentation())[0] == '\0',
       "ORIGINAL selected: both providers resolve ORIGINAL");
    sl_presentation_step(1);                       /* as an editor's RIGHT does */
    ck(sl_presentation_textures(sl_presentation()) == SL_TEXTURES_COMMUNITY
       && sl_presentation_world_set(sl_presentation())[0] == '\0',
       "one step: the texture provider moved, the world provider stayed dark");
    sl_presentation_step(1);                       /* as F5 does */
    ck(sl_presentation_textures(sl_presentation()) == SL_TEXTURES_XBLA
       && strcmp(sl_presentation_world_set(sl_presentation()), "xbla") == 0,
       "one more: BOTH providers moved together - the coordination this round adds");
    sl_settings_set(SL_SET_PRESENTATION, SL_PRESENTATION_ORIGINAL);  /* the store directly */
    ck(sl_presentation() == SL_PRESENTATION_ORIGINAL
       && sl_presentation_world_set(sl_presentation())[0] == '\0',
       "a write straight to the store row is seen by the policy - no cached duplicate");

    /* 8. THE PRESENTATION FOG RANGE (D-025): exactly one row is lengthened,
     *    and only under the set that names it. */
    ck(sl_presentation_fog_end("xbla", 33u, 15000.0f) == 54537.0f,
       "xbla Dam gameplay (33): authored 15000 is presented as 54537");
    ck(sl_presentation_fog_end("xbla", 33u, 15000.0f) < 75000.0f,
       "the presented Dam range fogs out inside the far plane (75000 world units)");
    ck(sl_presentation_fog_end("xbla", 933u, 75000.0f) == 75000.0f
       && sl_presentation_fog_end("xbla", 233u, 75000.0f) == 75000.0f,
       "the Dam intro cinema row and a multi-player row keep their authored end");
    ck(sl_presentation_fog_end("xbla", 35u, 55000.0f) == 55000.0f
       && sl_presentation_fog_end("xbla", 34u, 5000.0f) == 5000.0f,
       "another stage's row keeps its authored end");
    ck(sl_presentation_fog_end("", 33u, 15000.0f) == 15000.0f
       && sl_presentation_fog_end("community", 33u, 15000.0f) == 15000.0f
       && sl_presentation_fog_end((const char *) 0, 33u, 15000.0f) == 15000.0f,
       "no set, another set or a NULL set keeps the authored end");
    ck(sl_presentation_fog_end(sl_presentation_world_set(SL_PRESENTATION_ORIGINAL),
                               33u, 15000.0f) == 15000.0f
       && sl_presentation_fog_end(sl_presentation_world_set(SL_PRESENTATION_COMMUNITY),
                                  33u, 15000.0f) == 15000.0f
       && sl_presentation_fog_end(sl_presentation_world_set(SL_PRESENTATION_XBLA),
                                  33u, 15000.0f) == 54537.0f,
       "resolved through the presentations: only XBLA lengthens the Dam range");

    /* 9. SURFACE (D-029): the gameplay row is shortened to the game's own prop
     *    cut, and to no farther. */
    ck(sl_presentation_fog_end("xbla", 36u, 45000.0f) == 12071.0f,
       "xbla Surface gameplay (36): authored 45000 is presented as 12071");
    ck(sl_presentation_fog_end("xbla", 36u, 45000.0f) <= 12071.2f,
       "the presented Surface end is not beyond the game's far-fog prop cut "
       "(zDepth 12071.2 world units)");
    ck(sl_presentation_fog_end("xbla", 37u, 2500.0f) == 2500.0f
       && sl_presentation_fog_end("xbla", 236u, 45000.0f) == 45000.0f,
       "Jungle's row (37) and a multi-player row keep their authored end");
    ck(sl_presentation_fog_end("xbla", 43u, 10000.0f) == 7662.0f
       && sl_presentation_fog_end("xbla", 43u, 10000.0f) <= 7662.5f,
       "xbla Surface 2 gameplay (43): authored 10000 is presented at the game's "
       "far-fog prop cut (zDepth 7662.5 world units), no farther");
    ck(sl_presentation_fog_end("xbla", 943u, 40000.0f) == 40000.0f,
       "Surface 2's intro cinema row (943) keeps its authored end");
    ck(sl_presentation_fog_end(sl_presentation_world_set(SL_PRESENTATION_ORIGINAL),
                               43u, 10000.0f) == 10000.0f
       && sl_presentation_fog_end(sl_presentation_world_set(SL_PRESENTATION_COMMUNITY),
                                  43u, 10000.0f) == 10000.0f,
       "ORIGINAL and COMMUNITY HD keep Surface 2's authored end");
    ck(sl_presentation_fog_end(sl_presentation_world_set(SL_PRESENTATION_ORIGINAL),
                               36u, 45000.0f) == 45000.0f
       && sl_presentation_fog_end(sl_presentation_world_set(SL_PRESENTATION_COMMUNITY),
                                  36u, 45000.0f) == 45000.0f
       && sl_presentation_fog_end((const char *) 0, 36u, 45000.0f) == 45000.0f,
       "ORIGINAL, COMMUNITY HD and no set keep Surface's authored end");

    /* 9a. STATUE (D-043): the gameplay row's start moves out, under the xbla
     *     set alone; its end, every other row and every other set stay. */
    ck(sl_presentation_fog_start("xbla", 22u, -100.0f) == 800.0f,
       "xbla Statue gameplay (22): authored start -100 is presented at 800");
    ck(sl_presentation_fog_start("xbla", 22u, -100.0f) < 2000.0f,
       "the presented Statue start is nearer than the game's own NearFog (2000)");
    ck(sl_presentation_fog_end("xbla", 22u, 3500.0f) == 3500.0f,
       "Statue's end stays authored (3500), past the game's prop cut (3476.3)");
    ck(sl_presentation_fog_start("xbla", 33u, -100.0f) == -100.0f
       && sl_presentation_fog_start("xbla", 36u, -100.0f) == -100.0f
       && sl_presentation_fog_start("xbla", 43u, -100.0f) == -100.0f
       && sl_presentation_fog_start("xbla", 222u, -100.0f) == -100.0f
       && sl_presentation_fog_start("xbla", 122u, -100.0f) == -100.0f,
       "the Dam, Surface, Surface 2 and Statue's other rows keep their authored start");
    ck(sl_presentation_fog_start(sl_presentation_world_set(SL_PRESENTATION_ORIGINAL),
                                 22u, -100.0f) == -100.0f
       && sl_presentation_fog_start(sl_presentation_world_set(SL_PRESENTATION_COMMUNITY),
                                    22u, -100.0f) == -100.0f
       && sl_presentation_fog_start((const char *) 0, 22u, -100.0f) == -100.0f
       && sl_presentation_fog_start(sl_presentation_world_set(SL_PRESENTATION_XBLA),
                                    22u, -100.0f) == 800.0f,
       "only PRESENTATION XBLA reaches Statue's start");

    /* 9b. EGYPTIAN'S FADE (D-055): the second environment's range moves to
     *     345 .. 1550 under the xbla set alone; Egyptian's own row, every
     *     other set and the unset switch's default stay as stated. */
    ck(sl_presentation_fog_start("xbla", 132u, -100.0f) == 345.0f
       && sl_presentation_fog_end("xbla", 132u, 10000.0f) == 1550.0f,
       "xbla Egyptian's second environment (132): -100 .. 10000 presented at 345 .. 1550");
    ck(sl_presentation_fog_start("xbla", 32u, -100.0f) == -100.0f
       && sl_presentation_fog_end("xbla", 32u, 20000.0f) == 20000.0f,
       "Egyptian's own row (32) stays authored, so nothing changes before the fade");
    ck(sl_presentation_fog_start(sl_presentation_world_set(SL_PRESENTATION_ORIGINAL),
                                 132u, -100.0f) == -100.0f
       && sl_presentation_fog_end(sl_presentation_world_set(SL_PRESENTATION_COMMUNITY),
                                  132u, 10000.0f) == 10000.0f
       && sl_presentation_fog_end((const char *) 0, 132u, 10000.0f) == 10000.0f,
       "only PRESENTATION XBLA reaches Egyptian's second environment");
    ck(fog_row_on((const char *) 0) && fog_row_on("SL_PRESENTATION_SELFTEST_UNSET_SWITCH"),
       "a row with no switch, or with its switch unset, is applied");

    /* 10. THE GAME'S ATMOSPHERE (D-030): Surface's gameplay row only, and
     *     only in the xbla set. */
    ck(sl_presentation_game_atmosphere("xbla", 36u),
       "xbla Surface gameplay (36) draws the game's atmosphere");
    ck(!sl_presentation_game_atmosphere("xbla", 33u)
       && !sl_presentation_game_atmosphere("xbla", 35u)
       && !sl_presentation_game_atmosphere("xbla", 34u),
       "the Dam, Runway and Facility keep the remaster's own atmosphere");
    ck(!sl_presentation_game_atmosphere("xbla", 236u)
       && !sl_presentation_game_atmosphere("xbla", 936u)
       && !sl_presentation_game_atmosphere("xbla", 933u),
       "a multi-player row and another stage's cinema rows are not named");
    ck(sl_presentation_game_atmosphere("xbla", 43u)
       && sl_presentation_game_atmosphere("xbla", 943u),
       "xbla Surface 2 gameplay (43) and its intro cinema (943) draw the game's atmosphere");
    ck(!sl_presentation_game_atmosphere(sl_presentation_world_set(SL_PRESENTATION_ORIGINAL), 43u)
       && !sl_presentation_game_atmosphere(sl_presentation_world_set(SL_PRESENTATION_COMMUNITY), 943u),
       "ORIGINAL and COMMUNITY HD do not reach Surface 2's");
    ck(!sl_presentation_game_atmosphere(sl_presentation_world_set(SL_PRESENTATION_ORIGINAL), 36u)
       && !sl_presentation_game_atmosphere(sl_presentation_world_set(SL_PRESENTATION_COMMUNITY), 36u)
       && !sl_presentation_game_atmosphere((const char *) 0, 36u)
       && sl_presentation_game_atmosphere(sl_presentation_world_set(SL_PRESENTATION_XBLA), 36u),
       "only PRESENTATION XBLA reaches it");

    /* 10a. PROPS DRAWN PAST THE FAR-FOG CUT (D-041): Surface 2's gameplay
     *      row only, and only in the xbla set. */
    ck(sl_presentation_prop_fog_drawn("xbla", 43u),
       "xbla Surface 2 gameplay (43) draws props past the game's far-fog cut");
    ck(!sl_presentation_prop_fog_drawn("xbla", 36u),
       "Surface's row (36) keeps the cut its presented end is placed on (D-029)");
    ck(!sl_presentation_prop_fog_drawn("xbla", 943u)
       && !sl_presentation_prop_fog_drawn("xbla", 33u)
       && !sl_presentation_prop_fog_drawn("xbla", 34u)
       && !sl_presentation_prop_fog_drawn("xbla", 243u),
       "Surface 2's cinema row, the Dam, Facility and a multi-player row keep the cut");
    ck(!sl_presentation_prop_fog_drawn(sl_presentation_world_set(SL_PRESENTATION_ORIGINAL), 43u)
       && !sl_presentation_prop_fog_drawn(sl_presentation_world_set(SL_PRESENTATION_COMMUNITY), 43u)
       && !sl_presentation_prop_fog_drawn((const char *) 0, 43u)
       && sl_presentation_prop_fog_drawn(sl_presentation_world_set(SL_PRESENTATION_XBLA), 43u),
       "only PRESENTATION XBLA reaches it");

    /* 11. A BORROWED SKY (D-031): Bunker 1 draws Surface's sky under
     *     Surface's row, in the xbla set only. */
    {
        unsigned row = 0u;
        const char *s = sl_presentation_sky_borrow("xbla", "bunker", &row);
        ck(s != (const char *) 0 && strcmp(s, "surface") == 0 && row == 36u,
           "xbla Bunker 1 (bunker) draws skydome surface under environment row 36");
        row = 7u;
        ck(sl_presentation_sky_borrow("xbla", "surface", &row) == (const char *) 0
           && sl_presentation_sky_borrow("xbla", "dam", &row) == (const char *) 0
           && sl_presentation_sky_borrow("xbla", "bunker2", &row) == (const char *) 0
           && sl_presentation_sky_borrow("xbla", "facility", &row) == (const char *) 0
           && row == 7u,
           "Surface, the Dam, Bunker 2 and Facility borrow nothing, and the row is untouched");
        ck(sl_presentation_sky_borrow(sl_presentation_world_set(SL_PRESENTATION_ORIGINAL),
                                      "bunker", (unsigned *) 0) == (const char *) 0
           && sl_presentation_sky_borrow(sl_presentation_world_set(SL_PRESENTATION_COMMUNITY),
                                         "bunker", (unsigned *) 0) == (const char *) 0
           && sl_presentation_sky_borrow((const char *) 0, "bunker", (unsigned *) 0)
              == (const char *) 0
           && sl_presentation_sky_borrow("xbla", (const char *) 0, (unsigned *) 0)
              == (const char *) 0
           && sl_presentation_sky_borrow(sl_presentation_world_set(SL_PRESENTATION_XBLA),
                                         "bunker", (unsigned *) 0) != (const char *) 0,
           "only PRESENTATION XBLA reaches it");
    }

    /* 11a. A LENT DOME (D-044): Streets draws Frigate's cloud-cover dome
     *      under its own row 29, Depot under 30 and Cradle under 41, in the
     *      xbla set only. */
    {
        const char *s = sl_presentation_sky_lent("xbla", "streets");
        ck(s != (const char *) 0 && strcmp(s, "frigate") == 0,
           "xbla Streets (streets) draws skydome frigate");
        ck(sl_presentation_game_atmosphere("xbla", 29u),
           "xbla Streets gameplay (29) shades it with the game's atmosphere");
        s = sl_presentation_sky_lent("xbla", "depot");
        ck(s != (const char *) 0 && strcmp(s, "frigate") == 0,
           "xbla Depot (depot) draws skydome frigate (#92)");
        ck(sl_presentation_game_atmosphere("xbla", 30u),
           "xbla Depot gameplay (30) shades it with the game's atmosphere");
        ck(!sl_presentation_game_atmosphere("xbla", 230u)
           && !sl_presentation_game_atmosphere("xbla", 930u)
           && !sl_presentation_game_atmosphere("xbla", 31u),
           "a Depot multi-player or cinema row, and the next stage's, are not named");
        ck(sl_presentation_sky_lent(sl_presentation_world_set(SL_PRESENTATION_ORIGINAL),
                                    "depot") == (const char *) 0
           && sl_presentation_sky_lent(sl_presentation_world_set(SL_PRESENTATION_COMMUNITY),
                                       "depot") == (const char *) 0
           && !sl_presentation_game_atmosphere(sl_presentation_world_set(SL_PRESENTATION_ORIGINAL), 30u)
           && !sl_presentation_game_atmosphere(sl_presentation_world_set(SL_PRESENTATION_COMMUNITY), 30u),
           "ORIGINAL and COMMUNITY HD lend Depot nothing");
        s = sl_presentation_sky_lent("xbla", "cradle");
        ck(s != (const char *) 0 && strcmp(s, "frigate") == 0,
           "xbla Cradle (cradle) draws skydome frigate (#96)");
        ck(sl_presentation_game_atmosphere("xbla", 41u),
           "xbla Cradle gameplay (41) shades it with the game's atmosphere");
        ck(!sl_presentation_game_atmosphere("xbla", 241u)
           && !sl_presentation_game_atmosphere("xbla", 941u)
           && !sl_presentation_game_atmosphere("xbla", 40u)
           && !sl_presentation_game_atmosphere("xbla", 42u),
           "a Cradle multi-player or cinema row, and its neighbours', are not named");
        ck(sl_presentation_sky_lent(sl_presentation_world_set(SL_PRESENTATION_ORIGINAL),
                                    "cradle") == (const char *) 0
           && sl_presentation_sky_lent(sl_presentation_world_set(SL_PRESENTATION_COMMUNITY),
                                       "cradle") == (const char *) 0
           && !sl_presentation_game_atmosphere(sl_presentation_world_set(SL_PRESENTATION_ORIGINAL), 41u)
           && !sl_presentation_game_atmosphere(sl_presentation_world_set(SL_PRESENTATION_COMMUNITY), 41u),
           "ORIGINAL and COMMUNITY HD lend Cradle nothing");
        ck(sl_presentation_sky_lent("xbla", "cavern") == (const char *) 0
           && sl_presentation_sky_lent("xbla", "control") == (const char *) 0
           && sl_presentation_sky_lent("xbla", "jungle") == (const char *) 0,
           "Caverns, Control and Jungle are lent nothing");
        ck(sl_presentation_sky_lent("xbla", "statuepark") == (const char *) 0
           && sl_presentation_sky_lent("xbla", "archives") == (const char *) 0
           && sl_presentation_sky_lent("xbla", "bunker") == (const char *) 0
           && sl_presentation_sky_lent("xbla", "surface") == (const char *) 0
           && sl_presentation_sky_lent("xbla", "frigate") == (const char *) 0,
           "Statue, Archives, Bunker 1, Surface and Frigate are lent nothing");
        ck(!sl_presentation_game_atmosphere("xbla", 229u)
           && !sl_presentation_game_atmosphere("xbla", 929u)
           && !sl_presentation_game_atmosphere("xbla", 22u)
           && !sl_presentation_game_atmosphere("xbla", 24u),
           "a Streets multi-player or cinema row, Statue and Archives are not named");
        ck(sl_presentation_sky_lent(sl_presentation_world_set(SL_PRESENTATION_ORIGINAL),
                                    "streets") == (const char *) 0
           && sl_presentation_sky_lent(sl_presentation_world_set(SL_PRESENTATION_COMMUNITY),
                                       "streets") == (const char *) 0
           && sl_presentation_sky_lent((const char *) 0, "streets") == (const char *) 0
           && sl_presentation_sky_lent("xbla", (const char *) 0) == (const char *) 0
           && !sl_presentation_game_atmosphere(sl_presentation_world_set(SL_PRESENTATION_ORIGINAL), 29u)
           && !sl_presentation_game_atmosphere(sl_presentation_world_set(SL_PRESENTATION_COMMUNITY), 29u)
           && sl_presentation_sky_lent(sl_presentation_world_set(SL_PRESENTATION_XBLA),
                                       "streets") != (const char *) 0,
           "only PRESENTATION XBLA reaches it");
    }

    /* 12. THE GAME'S ATMOSPHERE ON A FOGLESS STAGE (D-034): Frigate's dome,
     *     in the xbla set only. */
    ck(sl_presentation_fogless_atmosphere("xbla", "frigate"),
       "xbla Frigate (frigate) shades its dome with the game's fogless record");
    ck(!sl_presentation_fogless_atmosphere("xbla", "bunker")
       && !sl_presentation_fogless_atmosphere("xbla", "silo")
       && !sl_presentation_fogless_atmosphere("xbla", "surface")
       && !sl_presentation_fogless_atmosphere("xbla", "dam"),
       "Bunker 1, Silo, Surface and the Dam are not named");
    ck(!sl_presentation_fogless_atmosphere(sl_presentation_world_set(SL_PRESENTATION_ORIGINAL), "frigate")
       && !sl_presentation_fogless_atmosphere(sl_presentation_world_set(SL_PRESENTATION_COMMUNITY), "frigate")
       && !sl_presentation_fogless_atmosphere((const char *) 0, "frigate")
       && !sl_presentation_fogless_atmosphere("xbla", (const char *) 0)
       && sl_presentation_fogless_atmosphere(sl_presentation_world_set(SL_PRESENTATION_XBLA), "frigate"),
       "only PRESENTATION XBLA reaches it");

    /* 13. THE FITTED ITEM (D-038): the C4, in the xbla set only. */
    ck(sl_presentation_prop_fit("xbla", "chrplastique"),
       "xbla fits prop/chrplastique onto the game's C4");
    ck(!sl_presentation_prop_fit("xbla", "chrmp5k")
       && !sl_presentation_prop_fit("xbla", "desk1")
       && !sl_presentation_prop_fit("xbla", "chrplastiquex")
       && !sl_presentation_prop_fit("xbla", "chrbombcase"),
       "no other item is fitted");
    ck(!sl_presentation_prop_fit(sl_presentation_world_set(SL_PRESENTATION_ORIGINAL), "chrplastique")
       && !sl_presentation_prop_fit(sl_presentation_world_set(SL_PRESENTATION_COMMUNITY), "chrplastique")
       && !sl_presentation_prop_fit((const char *) 0, "chrplastique")
       && !sl_presentation_prop_fit("xbla", (const char *) 0)
       && sl_presentation_prop_fit(sl_presentation_world_set(SL_PRESENTATION_XBLA), "chrplastique"),
       "only PRESENTATION XBLA reaches it");

    /* 13a. A ONE-PIECE ITEM OVER A MODEL THAT MOVES PARTS (D-045): the jeep,
     *      in the xbla set only. */
    ck(sl_presentation_prop_rigid("xbla", "jeep"),
       "xbla lets prop/jeep ride PjeepZ's root matrix whole");
    ck(!sl_presentation_prop_rigid("xbla", "miltruck")
       && !sl_presentation_prop_rigid("xbla", "tank")
       && !sl_presentation_prop_rigid("xbla", "plane")
       && !sl_presentation_prop_rigid("xbla", "chrplastique"),
       "the truck, the tank, the plane and the C4 are not named");
    ck(!sl_presentation_prop_rigid(sl_presentation_world_set(SL_PRESENTATION_ORIGINAL), "jeep")
       && !sl_presentation_prop_rigid(sl_presentation_world_set(SL_PRESENTATION_COMMUNITY), "jeep")
       && !sl_presentation_prop_rigid((const char *) 0, "jeep")
       && !sl_presentation_prop_rigid("xbla", (const char *) 0)
       && sl_presentation_prop_rigid(sl_presentation_world_set(SL_PRESENTATION_XBLA), "jeep"),
       "only PRESENTATION XBLA reaches it");

    /* 14. THE WITHHELD FILL: Frigate's boarding boat, in the xbla set only. */
    ck(sl_presentation_fill_withheld("xbla", "frigate", 57),
       "xbla withholds the native fill from Frigate's room 57 (the boarding boat)");
    ck(!sl_presentation_fill_withheld("xbla", "frigate", 56)
       && !sl_presentation_fill_withheld("xbla", "frigate", 4)
       && !sl_presentation_fill_withheld("xbla", "runway", 4)
       && !sl_presentation_fill_withheld("xbla", "runway", 57)
       && !sl_presentation_fill_withheld("xbla", "dam", 57),
       "no other room, and not Runway's door strip (room 4)");

    /* 14a. THE GAME'S DETAIL IN A NAMED ROOM (#96): Cradle's room 7, xbla only. */
    ck(sl_presentation_native_kept("xbla", "cradle", 7),
       "xbla keeps the game's translucent detail in Cradle's room 7 (the shaft's ladder)");
    ck(!sl_presentation_native_kept("xbla", "cradle", 8)
       && !sl_presentation_native_kept("xbla", "cradle", 34)
       && !sl_presentation_native_kept("xbla", "cradle", 14)
       && !sl_presentation_native_kept("xbla", "frigate", 7)
       && !sl_presentation_native_kept("xbla", "cavern", 7),
       "no other room of Cradle (not the arms' cables, rooms 34 .. 36, nor the rails, 10 and 14), "
       "no other stage");
    ck(!sl_presentation_native_kept(sl_presentation_world_set(SL_PRESENTATION_ORIGINAL), "cradle", 7)
       && !sl_presentation_native_kept(sl_presentation_world_set(SL_PRESENTATION_COMMUNITY), "cradle", 7)
       && !sl_presentation_native_kept((const char *) 0, "cradle", 7)
       && !sl_presentation_native_kept("xbla", (const char *) 0, 7)
       && sl_presentation_native_kept(sl_presentation_world_set(SL_PRESENTATION_XBLA), "cradle", 7),
       "only PRESENTATION XBLA reaches it");

    /* 15. SIGHTLINE'S OWN SEA (D-039): Frigate, in the xbla set only. */
    ck(sl_presentation_own_sea("xbla", "frigate"), "xbla draws Frigate's sea with its own program");
    ck(!sl_presentation_own_sea("xbla", "dam") && !sl_presentation_own_sea("xbla", "runway")
       && !sl_presentation_own_sea("xbla", "silo") && !sl_presentation_own_sea("xbla", "surface"),
       "no other stage");
    ck(!sl_presentation_own_sea(sl_presentation_world_set(SL_PRESENTATION_ORIGINAL), "frigate")
       && !sl_presentation_own_sea(sl_presentation_world_set(SL_PRESENTATION_COMMUNITY), "frigate")
       && !sl_presentation_own_sea((const char *) 0, "frigate")
       && !sl_presentation_own_sea("xbla", (const char *) 0)
       && sl_presentation_own_sea(sl_presentation_world_set(SL_PRESENTATION_XBLA), "frigate"),
       "only PRESENTATION XBLA reaches it - ORIGINAL and COMMUNITY HD draw the game's sea");
    ck(!sl_presentation_fill_withheld(sl_presentation_world_set(SL_PRESENTATION_ORIGINAL), "frigate", 57)
       && !sl_presentation_fill_withheld(sl_presentation_world_set(SL_PRESENTATION_COMMUNITY), "frigate", 57)
       && !sl_presentation_fill_withheld((const char *) 0, "frigate", 57)
       && !sl_presentation_fill_withheld("xbla", (const char *) 0, 57)
       && sl_presentation_fill_withheld(sl_presentation_world_set(SL_PRESENTATION_XBLA), "frigate", 57),
       "only PRESENTATION XBLA reaches it");

    /* 16. THE GAME'S FADE (D-035): Frigate, in the xbla set only. */
    ck(sl_presentation_game_fade("xbla", "frigate"), "xbla draws the game's fade on Frigate");
    ck(!sl_presentation_game_fade("xbla", "jungle") && !sl_presentation_game_fade("xbla", "train")
       && !sl_presentation_game_fade("xbla", "statuepark") && !sl_presentation_game_fade("xbla", "dam"),
       "not on Jungle, whose backdrop cards pass the shade test, nor Train, Statue or the Dam");
    ck(!sl_presentation_game_fade(sl_presentation_world_set(SL_PRESENTATION_ORIGINAL), "frigate")
       && !sl_presentation_game_fade(sl_presentation_world_set(SL_PRESENTATION_COMMUNITY), "frigate")
       && !sl_presentation_game_fade((const char *) 0, "frigate")
       && !sl_presentation_game_fade("xbla", (const char *) 0)
       && sl_presentation_game_fade(sl_presentation_world_set(SL_PRESENTATION_XBLA), "frigate"),
       "only PRESENTATION XBLA reaches it");

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
#endif /* SL_PRESENTATION_SELFTEST */
