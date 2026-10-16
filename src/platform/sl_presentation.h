/**
 * sl_presentation.h - the PRESENTATION POLICY (#68): the ONE player-facing
 * visual selection, and the per-provider selections it RESOLVES.
 *
 * WHY THIS FILE EXISTS. Before it, the player had two unrelated controls: a
 * persisted TEXTURES setting (#47) that moved the texture provider, and a
 * developer environment switch SL_WORLD_ASSETS (#68) that moved the
 * world-asset provider. Nothing tied them together, so a player could reach
 * ORIGINAL textures with an XBLA skydome, or COMMUNITY HD textures with an
 * XBLA skydome - states nobody designed and nobody wants to look at. The
 * design decision (2026-09-23) is COHERENT BUNDLES: one selection, and every
 * visual provider moves with it.
 *
 * WHAT IT IS NOT. It is NOT the texture provider growing meshes. The low-level
 * providers stay exactly what they were - one answers "what artwork goes on
 * this game texture" (sl_gfx_texprov.h), the other "is there an external world
 * asset of this class for this item" (sl_gfx_worldasset.h) - with their own
 * identities, their own roots, their own formats and their own caches. This
 * file adds a layer ABOVE them that says WHICH of their sets is selected. A
 * provider asks the policy; no system inspects another system's setting.
 *
 *     presentation  --resolves-->  { texture provider set,
 *                                    world-asset provider set,
 *                                    later: model / material / lighting }
 *
 * THE THREE PRESENTATIONS today, and the whole of the mapping:
 *
 *   ORIGINAL      textures ORIGINAL       world assets  none
 *   COMMUNITY HD  textures COMMUNITY HD   world assets  none
 *   XBLA          textures XBLA           world assets  xbla
 *
 * COMMUNITY HD carries no world assets because none exist for it: the
 * community pack is a texture pack. That is a fact about what is installed,
 * not a hole in the design - the resolver answers "none" and the Dam draws its
 * own sky, which is the correct picture for that presentation.
 *
 * FALLBACK STAYS PER PROVIDER AND NEVER CROSSES SETS. The policy chooses a set
 * per provider; what happens when that set is incomplete is the provider's own
 * business and is unchanged by this file: a texture the XBLA set lacks falls
 * back to ORIGINAL, never to COMMUNITY HD; a world asset the XBLA set lacks
 * draws the game's own picture, never another set's. The policy never
 * substitutes one presentation's set for another's, so a half-installed XBLA
 * pack degrades towards the original artwork and never towards the community
 * one.
 *
 * HOW A PROVIDER CLASS JOINS LATER, without touching F5. Add a resolver here
 * (sl_presentation_<class>_set) and one table column below; the new provider
 * reads it the way the two existing ones do. The cycle action, the two UI
 * surfaces, the stored key and the migration are all expressed in terms of the
 * presentation, never in terms of a provider, so none of them changes. This
 * round deliberately creates NO empty providers: model, material and lighting
 * (#48) providers do not exist yet and are not stubbed. Lighting in particular
 * is NOT pre-coupled to XBLA - when a lighting policy exists, this is where it
 * would resolve, and that is the only claim made here.
 *
 * THE CANONICAL VALUE. One runtime value and one stored key, `presentation`,
 * in the native settings store (sl_settings.h, SL_SET_PRESENTATION). Every
 * surface - the front end's SETTINGS -> DISPLAY row, the watch's SIGHTLINE ->
 * GRAPHICS row and the PRESENTATION cycle action on F5 / pad BACK - reads and
 * writes THAT row through this file and holds no copy of its own, which is
 * what makes a change in one visible in the others when they next draw.
 *
 * HOST-CLEAN: plain ints and const char * only, no game, SDL or GL type, so
 * src/platform, src/gfx and src/native (the N64 include tree) can all use it.
 * src/game does not include it; options.c declares what it calls, as it
 * already does for every other native accessor.
 */
#ifndef SL_PRESENTATION_H
#define SL_PRESENTATION_H

/* The presentations' persisted ids (APPEND-ONLY - the stored key carries
 * them, and #47-era configs carry the same three values under the legacy
 * `textures` key, which is what makes the migration a straight copy). */
#define SL_PRESENTATION_ORIGINAL   0
#define SL_PRESENTATION_COMMUNITY  1
#define SL_PRESENTATION_XBLA       2
#define SL_PRESENTATION_COUNT      3

/* The player-facing names, "ORIGINAL" / "COMMUNITY HD" / "XBLA"; "?" out of
 * range. Deliberately the same three words the TEXTURES row used to show, so
 * the HUD line a returning player sees after F5 reads the same - what changed
 * is that it now names the whole presentation, not one provider. */
const char *sl_presentation_name(int presentation);

/* ---- the canonical value ------------------------------------------------- */

/* The presentation in force. Reads the store's `presentation` row; an
 * INACTIVE store (trace replay, headless), a missing key, a malformed value
 * or one out of range all answer ORIGINAL. */
int  sl_presentation(void);

/* Select one. Out of range is refused (the value is left alone), exactly as
 * the store refuses an out-of-range set. Persists through the store's normal
 * write-on-change. */
void sl_presentation_set(int presentation);

/* The step both editors and the cycle action use: the next presentation in
 * the list, wrapping both ways. dir >= 0 steps forward. Forward from XBLA is
 * ORIGINAL, so three presses of F5 return where they started. */
void sl_presentation_step(int dir);

/* ---- the resolution: presentation -> per-provider selections -------------
 *
 * PURE FUNCTIONS OF THE ARGUMENT, not of the store, so the mapping can be
 * tested exhaustively without a config file and so a caller can ask "what
 * WOULD XBLA select" without changing anything. An out-of-range presentation
 * resolves as ORIGINAL would, which is the same refusal the rest of the
 * stack makes. */

/* Which TEXTURE SET (sl_settings.h SL_TEXTURES_*) this presentation selects.
 * The one caller is the texture provider's policy, sl_texprov_active. */
int sl_presentation_textures(int presentation);

/* Which WORLD-ASSET SET this presentation selects, as the provider's own set
 * name: "xbla", or "" for no world assets at all (which is not "a set that is
 * empty" - it is the provider dark, no root resolved and no file opened).
 * Never NULL. The one caller is the world-asset provider's policy,
 * sl_wa_active. */
const char *sl_presentation_world_set(int presentation);

/* ---- presentation fog range (D-025) --------------------------------------
 *
 * The stage-fog END a world-asset set draws one environment row with, given
 * the row's AUTHORED end (the value the set's own environment metadata
 * carries - SLEN, sl_gfx_worldasset.h). A presentation may lengthen the
 * range of a row where the authored range turns scenery the stage is built
 * around into flat fog colour, or shorten it where the game removes objects
 * the authored range still shows; it never changes the row's colour, its
 * start or the distance equation, and it never touches a row it does not
 * name.
 *
 * FOUR ROWS, all of the xbla set (sl_presentation.c has the derivations):
 *  - the Dam's gameplay row (environment 33, authored end 15000 world units)
 *    is drawn to 54537, placed on the cartridge's own Dam fog curve, so the
 *    mountain range and the far towers read through the haze and the scene
 *    still fogs out inside the far plane (D-025);
 *  - Surface's gameplay row (environment 36, authored end 45000) is drawn to
 *    12071, and Surface 2's (43, authored 10000) to 7662 - the distances at
 *    which the game's far-fog cut stops drawing props and characters, so
 *    nothing the game removes is still showing (D-029);
 *  - Egyptian's second environment (132, authored -100 .. 10000), the row
 *    the game fades to after Baron Samedi's second death, is drawn from 345
 *    to 1550, so the rooms close into black across the fade as the
 *    cartridge's do (D-055, with sl_presentation_fog_start).
 * Every other row - the Dam's intro cinema row (933), Egyptian's own (32) and
 * the multi-player rows - keeps its authored range.
 *
 * A function of its arguments and, for a row that has one, its own developer
 * switch (D-055's SL_XBLA_FADE_FOG=0 sets its row aside), so the self-test
 * pins it and the provider can ask it once per row as it loads the set. The
 * authored value is kept by the provider beside the presented one, and a
 * developer switch there (SL_XBLA_AUTHENTIC_FOG=1) draws every authored
 * range. A NULL or unknown set, or an id the table does not name, answers
 * `authored_end` unchanged. */
float sl_presentation_fog_end(const char *world_set, unsigned env_id,
                              float authored_end);

/* ---- presentation fog start (D-043) ---------------------------------------
 *
 * The stage-fog START a world-asset set draws one environment row with, given
 * the row's authored start: a presentation may move the start out where the
 * authored row fogs the near scene the game leaves clear. It never changes
 * the row's colour, its end or the distance equation, and never touches a
 * row it does not name.
 *
 * TWO ROWS, of the xbla set: Statue's gameplay row (environment 22, authored
 * start -100, end 3500) starts at 800 world units (D-043), and Egyptian's
 * second environment (132) at 345, where the game's own fog on that row
 * begins (D-055, with its presented end); sl_presentation.c has the
 * measurements. Answered as sl_presentation_fog_end answers (D-055's row
 * honours SL_XBLA_FADE_FOG=0); the provider keeps the authored start beside
 * the presented one, and the developer switches there (SL_XBLA_FOG_START=0,
 * or SL_XBLA_AUTHENTIC_FOG=1 for every presented range) draw it. */
float sl_presentation_fog_start(const char *world_set, unsigned env_id,
                                float authored_start);

/* ---- the game's atmosphere (D-030) -----------------------------------------
 *
 * Whether a world-asset set draws one environment row's ATMOSPHERE from the
 * game rather than from the set: the stage-fog colour is the colour the
 * game's environment holds (the one its own sky fills with and every fogged
 * draw fades to) instead of the set's authored row colour, and the set's sky
 * dome is shaded the way the game shades its own cloud layer (sky.c). The
 * range stays sl_presentation_fog_end's.
 *
 * ONE ROW, of the xbla set: Surface's gameplay row (environment 36). The
 * remaster's row for it is a warm grey-beige (164 150 130) under a dome that
 * is the same clear-blue image every stage of the source carries, so its haze
 * and its sky belong to no storm; the game's own row is a cold blue-grey
 * (96 96 128) under a cloud layer it tints itself (sl_presentation.c).
 * Every other row, set and presentation answers 0.
 *
 * Pure, like sl_presentation_fog_end. A developer switch in the provider
 * (SL_XBLA_GAME_ATMOSPHERE=0) draws the set's own atmosphere. */
int sl_presentation_game_atmosphere(const char *world_set, unsigned env_id);

/* ---- props drawn past the game's far-fog cut (D-041) -----------------------
 *
 * Whether a world-asset set draws the game's props past the game's far-fog
 * cut while the game holds environment row `env_id`: the prop is drawn where
 * fogGetPropDistColor (bgfog.c) would have it removed, and the set's row fogs
 * it per vertex as it fogs the background behind it (sl_wa_object_fog_env),
 * so a large prop comes out of the fog with the world around it instead of
 * appearing at the cut, which tests its origin only. Render-only: whether the
 * game counts the prop on screen, and every other gate of its draw, stay the
 * game's.
 *
 * ONE ROW, of the xbla set: Surface 2's gameplay row (environment 43). Every
 * other row, set and presentation answers 0 - Surface's row 36 keeps the cut
 * and the end placed on it (D-029).
 *
 * Pure, like the two above. A developer switch in the provider
 * (SL_XBLA_FOGGED_PROPS=0) keeps the game's cut. */
int sl_presentation_prop_fog_drawn(const char *world_set, unsigned env_id);

/* ---- a borrowed sky (D-031) ------------------------------------------------
 *
 * The skydome item a world-asset set draws for a stage (named by its item
 * path) that has no sky of its own, and in *env_id the environment row whose
 * atmosphere shades it; NULL for every other set, item and presentation, and
 * *env_id is then left alone.
 *
 * ONE ENTRY, of the xbla set: Bunker 1 (`bunker`) draws Surface 1's sky -
 * skydome `surface` under environment row 36, the dome, its D-023 cloud
 * field and D-030's game atmosphere exactly as Surface draws them - wherever
 * the outside shows (the start shaft's grate). A deliberate continuity
 * change: the game's Bunker 1 has no environment row and fills with navy.
 * The sky only: no fog, range or colour of row 36 reaches the stage.
 *
 * Pure, like the two above. The renderer asks only while the game holds no
 * environment row (the fogless record), and a developer switch in the
 * provider (SL_XBLA_BORROWED_SKY=0) keeps the game's fill. */
const char *sl_presentation_sky_borrow(const char *world_set, const char *item,
                                       unsigned *env_id);

/* ---- a lent dome (D-044) ---------------------------------------------------
 *
 * The skydome item a world-asset set draws for a stage (named by its item
 * path) whose source carries no dome while the stage keeps an environment row
 * of its own; NULL for every other set, item and presentation. Unlike D-031
 * only the dome's images and geometry are lent: the atmosphere that shades it
 * is the stage's own row, read live (sl_presentation_game_atmosphere names
 * it), and so is the cloud layer its D-023 cap rides.
 *
 * TWO ENTRIES, of the xbla set: Streets (`streets`) and Depot (`depot`, #92)
 * draw skydome `frigate`, the one source dome prepared as cloud cover
 * (D-034), under rows 29 and 30.
 *
 * Pure, like the others. A developer switch in the provider
 * (SL_XBLA_LENT_SKY=0) keeps the game's own cloud layer. */
const char *sl_presentation_sky_lent(const char *world_set, const char *item);

/* ---- the game's atmosphere on a fogless stage (D-034) ----------------------
 *
 * Whether a world-asset set shades the sky dome of a stage (named by its item
 * path) as the game shades its own cloud layer while the game holds NO
 * environment row there - the fogless record, which still carries a sky
 * colour, a cloud colour and a cloud layer that sky.c draws. D-030's rule,
 * for the stage whose environment has no row to key it by: the colours are
 * the record the game loaded, read live, and nothing else of the stage moves
 * (no fog, range or fill).
 *
 * ONE ENTRY, of the xbla set: Frigate (`frigate`). The remaster's dome for it
 * is the clear daylight image every source dome carries, a band of snowy
 * mountains at its foot, over the game's night-navy sea; the game's own
 * record is navy 16 48 96 under a 230 230 230 cloud layer.
 *
 * Pure, like the three above. A developer switch in the provider
 * (SL_XBLA_GAME_ATMOSPHERE=0, D-030's) draws the dome as authored. */
int sl_presentation_fogless_atmosphere(const char *world_set, const char *item);

/* ---- a prop item the remaster authored as another object (D-038) --------
 *
 * Whether a world-asset set's prop item (named by its item, `prop/<item>`)
 * may be FITTED onto the game's model when its reference box is not the
 * model's (src/sl_chr_asset.h, THE FITTED ITEM): the item kept whole, scaled
 * to stand inside the model's box and seated on its bottom. Every other item
 * keeps the box test, and one that fails it draws as the game's.
 *
 * ONE ENTRY, of the xbla set: `chrplastique`, the C4 the game places on
 * Frigate's bridge and engine-room consoles - in the source a round mounted
 * device in both generations, not the game's brick.
 *
 * Pure. SL_WA_AS_AUTHORED has no say here (no preparation is involved);
 * SL_PROP_FIT=0 (sl_chr_asset.c) refuses the item as before. */
int sl_presentation_prop_fit(const char *world_set, const char *item);

/* ---- a one-piece item over a model that moves parts (D-045) ---------------
 *
 * Whether a world-asset set's prop item (named by its item, `prop/<item>`)
 * that is ONE RIGID PIECE may ride a game model that moves parts of itself
 * (more than one matrix): the item is related to the model's whole box and
 * drawn on the model's root matrix, so its moving parts hold the rest pose.
 * Every other one-piece item on such a model is refused as before and the
 * game's model draws.
 *
 * ONE ENTRY, of the xbla set: `jeep` - the remaster's item has no skeleton
 * and its wheels lie in the body's own draws.
 *
 * Pure. SL_PROP_RIGID=0 (sl_chr_asset.c) refuses the item as before. */
int sl_presentation_prop_rigid(const char *world_set, const char *item);

/* ---- a room the remaster remodelled whole takes no native fill ----------
 *
 * Whether the #78 native fill (src/gfx/sl_gfx_worldasset.h) is withheld from
 * a stage's room under a world-asset set: every triangle of the room is
 * suppressed under the replacement, as before the fill existed, however it
 * registers. For a room the remaster remodels as an object of its own, whose
 * game triangles register on the remodelled surfaces only by coincidence and
 * show past its silhouette when filled.
 *
 * ONE ENTRY, of the xbla set: Frigate (`frigate`) room 57, the boarding boat.
 *
 * Pure. SL_BG_FILL_WITHHELD=0 (sl_gfx_worldasset.c) fills the room as the
 * rule decides. */
int sl_presentation_fill_withheld(const char *world_set, const char *item, int room);

/* ---- the game's translucent detail the replacement left out (#96) --------
 *
 * Whether `room` of the stage `item` keeps the game's translucent room
 * triangles that the replacement has no surface for (src/gfx/sl_gfx_dl.c
 * nf_room_kept) under a world-asset set; every other room's are suppressed
 * with the rest of the rooms.
 *
 * ONE ENTRY, of the xbla set: Cradle (`cradle`) room 7, the shaft's ladder,
 * which the remaster leaves out. Pure. SL_BG_KEEP_ROOMS=0
 * (sl_gfx_worldasset.c) keeps nothing. */
int sl_presentation_native_kept(const char *world_set, const char *item, int room);

/* ---- the game's fade where the replacement has nothing (D-035) ----------
 *
 * Whether a stage draws the game's own fade - a translucent room triangle
 * whose shade spans dark to bright - where the replacement has no surface
 * (src/gfx/sl_gfx_dl.c nf_fade_kept, #83) under a world-asset set. On every
 * other stage those triangles are suppressed with the rest of the rooms.
 *
 * ONE ENTRY, of the xbla set: Frigate (`frigate`), the stage D-035 cuts at
 * the game's waterline. Pure. SL_BG_FADE_STAGES=0 (sl_gfx_worldasset.c)
 * draws the fade on every stage, the earlier rule. */
int sl_presentation_game_fade(const char *world_set, const char *item);

/* ---- Sightline's own sea (D-039) ----------------------------------------
 *
 * Whether a world-asset set draws a stage's sky.c sea with Sightline's own
 * program (src/gfx/sl_gfx_dl.c, the sea program) instead of the game's
 * texture-times-shade: the same image, shade and plane, in layers, with the
 * sky's reflection and its horizon colour.
 *
 * ONE ENTRY, of the xbla set: Frigate (`frigate`).
 *
 * Pure. SL_XBLA_SEA=0 (sl_gfx_worldasset.c) draws the game's sea. */
int sl_presentation_own_sea(const char *world_set, const char *item);

#endif /* SL_PRESENTATION_H */
