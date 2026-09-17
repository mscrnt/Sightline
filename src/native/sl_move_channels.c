/**
 * The four native movement channels, and the only state shared between the
 * platform input layer and bondviewProcessInput.
 *
 * Why a separate translation unit. src/platform compiles against the HOST
 * headers alone (see the header comment in sl_game_query.c), so it cannot see
 * struct MoveData; src/game is compiled by IDO for the matching build, so it
 * cannot see SDL. Neither side can hold this storage. src/native can: it is
 * built with the game include path but contains no game logic, and nothing in
 * src/game includes anything from here - the game declares the two entry
 * points locally, inside its own `#ifndef __sgi`, so IDO sees zero tokens and
 * the layering rule (src/game must not include from src/platform) is untouched.
 *
 * WHAT THIS IS FOR, and just as importantly what it is not for. Native
 * keyboard and mouse supply FOUR NUMBERS and nothing else:
 *
 *     walk    forward / back      analogWalk    (+ = forward)
 *     strafe  left / right        analogStrafe  (+ = right)
 *     turn    yaw rate            analogTurn    (+ = turn right, MEASURED)
 *     pitch   pitch rate          analogPitch   (+ = look down, MEASURED)
 *
 * in the game's own +/-70 unit - every consumer of these four divides by 70.0f
 * (bondview2.c:5635, :5654, :5902, :5960). They are written at the seam in
 * bondviewProcessInput AFTER the control-style branches have finished
 * interpreting the pad and BEFORE anything reads them, which is what makes
 * keyboard and mouse behave the same under every control style without this
 * layer knowing what a control style is.
 *
 * Everything else stays the game's: acceleration and deceleration curves,
 * collision, camera, look-ahead and auto-centring, pitch limits, the player's
 * own Look Up/Down option, aim mode, auto-aim, firing, weapons, crouch, lean,
 * the tank, and menus. Fire / aim / use / weapon-cycle still arrive as N64
 * BUTTONS and are still interpreted by the selected control style. No physical
 * control is bound directly to a semantic game action anywhere in this path.
 *
 * The sign conventions above are MEASURED, not read off the source - see
 * docs/decisions/native-input-signs.md for the run that established them.
 *
 * LIVE INPUT ONLY. `valid` is set by the platform layer only when there is a
 * window AND no recorded stream is loaded, which is the same condition that
 * gates every other live-input producer (sl_live_input_active(), the export of
 * sl_live_two_pads in sl_ultra_shim.c). Headless replay never sets it, so the
 * recorded four-bytes-per-retrace format is unchanged and trace determinism is
 * unaffected by construction. The gamepad does NOT come through here: it maps
 * onto the physical N64 pad and always has, and the two producers are kept
 * separate on purpose.
 */
#ifndef __sgi

/* Not a struct in a shared header on purpose: a header would have to be
 * includable from src/game, and the point of this file is that nothing in
 * src/game includes anything to reach it. */
static int g_valid;                 /* 1 while live keyboard/mouse owns these */
static int g_walk, g_strafe, g_turn, g_pitch;

/**
 * Publish this frame's channels. Called once per live poll from
 * src/platform/sl_input.c, with active = 0 whenever the override must not
 * apply (no live input, a menu is up, or a gamepad was the last device
 * touched).
 *
 * AIM MODE IS NOT ONE OF THOSE (changed 2026-09-03). It used to be: while the
 * game was aiming the platform layer withheld these four and drove the N64
 * stick with the mouse instead, and that is what made right-click aiming feel
 * digital - the game reads stick POSITION while aiming, for the crosshair
 * offset and for a turn that only starts past a hard +/-60, and a mouse
 * reports a RATE. The platform layer now publishes turn and pitch in both
 * modes and leaves walk and strafe at 0 while aiming; the seam in
 * bondviewProcessInput applies only turn and pitch while insightaimmode is
 * set, so everything else aim mode owns is untouched. Live keyboard/mouse
 * only - the pad does not come through here, so a controller keeps
 * GoldenEye's original floating manual aim. See docs/divergences.md.
 */
void sl_move_channels_set(int active, int walk, int strafe, int turn, int pitch)
{
    g_valid  = active != 0;
    g_walk   = walk;
    g_strafe = strafe;
    g_turn   = turn;
    g_pitch  = pitch;
}

/**
 * Read this frame's channels. Returns 0 - leaving the outputs untouched - when
 * the override does not apply, which is the case for every headless run and
 * every recorded replay. The caller (bondviewProcessInput) applies its own
 * gameplay gates on top; this one only answers "is there live keyboard/mouse
 * intent to apply at all".
 */
/**
 * The four channels AS PUBLISHED, validity included, for the RECORDER.
 *
 * sl_move_channels_get above is the game's reader and deliberately answers
 * "nothing to apply" by returning 0 and leaving the outputs alone. A recorder
 * needs the opposite: the withheld frames are data too, because "the player
 * let go" and "a menu was up" have to replay as themselves rather than as the
 * last frame that happened to be active. So this one always writes all five
 * outputs and never hides behind g_valid.
 *
 * Not folded into the getter, because changing the getter's contract would
 * change what the game seam does on a withheld frame - the one thing that must
 * not move. See sl_move_record_vi in src/platform/sl_ultra_shim.c.
 */
void sl_move_channels_peek(int *active, int *walk, int *strafe, int *turn,
                           int *pitch)
{
    if (active) *active = g_valid;
    if (walk)   *walk   = g_walk;
    if (strafe) *strafe = g_strafe;
    if (turn)   *turn   = g_turn;
    if (pitch)  *pitch  = g_pitch;
}

int sl_move_channels_get(int *walk, int *strafe, int *turn, int *pitch)
{
    if (!g_valid)
        return 0;
    if (walk)   *walk   = g_walk;
    if (strafe) *strafe = g_strafe;
    if (turn)   *turn   = g_turn;
    if (pitch)  *pitch  = g_pitch;
    return 1;
}


/* ---------------------------------------------------------------------------
 * THE LINEAR MOUSE LOOK CHANNEL, added 2026-09-07.
 *
 * A SECOND, SEPARATE pair of numbers, deliberately not folded into the four
 * above. The four are a RATE in the game's own +/-70 stick unit, and every
 * consumer divides by 70 and then SIGNED-SQUARES the result
 * (bondview2.c:6158-6178 for yaw, :6100-6120 for pitch). That curve is correct
 * for a stick, which reports a POSITION held over time, and wrong for a mouse,
 * which reports a DISPLACEMENT that has already happened.
 *
 * MEASURED consequence of pushing a mouse through the stick curve, and the
 * owner's reported symptom: equal-and-opposite motion does NOT return to the
 * starting angle when the two halves are delivered at different SPEEDS,
 * because squaring makes the total depend on how the displacement was
 * DISTRIBUTED ACROSS FRAMES rather than on its sum. 300 counts left over 5
 * frames then 300 counts right over 20 frames drifts -52.5 degrees per cycle.
 * Same counts, opposite directions, no return. See docs/divergences.md.
 *
 * So these two carry DEGREES, already scaled by sensitivity, unclamped and
 * unsquared, and the game applies them to vv_theta / vv_verta directly in the
 * frame they arrive. Yaw is + = right; pitch is + = look UP and is final -
 * the Look Up/Down controller option is deliberately NOT applied to it, which
 * is the standing "THE MOUSE OWNS ITS OWN PITCH" decision (sl_input.c map_kbm).
 *
 * THE PAD DOES NOT COME THROUGH HERE, exactly as it does not come through the
 * four channels: `active` is set only for live keyboard/mouse with actual
 * mouse motion this frame, so a controller keeps GoldenEye's square curve, its
 * ramp and its ceiling untouched. That is the whole safety argument for a
 * change inside src/game.
 * ------------------------------------------------------------------------- */
static int   g_ml_valid;            /* 1 while a live MOUSE owns look */
static float g_ml_yaw_deg;          /* + = turn right */
static float g_ml_pitch_deg;        /* + = look up, final */

/**
 * Publish this frame's linear mouse look. Called once per live poll from
 * src/platform/sl_input.c, with active = 0 whenever the linear path must not
 * apply - no live mouse, a menu is up, the pointer is not captured, the
 * gamepad was the last device touched, or SL_MOUSE_LINEAR_LOOK=0.
 */
void sl_mouse_look_set(int active, float yaw_deg, float pitch_deg)
{
    g_ml_valid     = active != 0;
    g_ml_yaw_deg   = yaw_deg;
    g_ml_pitch_deg = pitch_deg;
}

/**
 * Read this frame's linear mouse look. Returns 0 - leaving the outputs
 * untouched - whenever the linear path does not apply, which is the case for
 * every headless run, every recorded replay and every gamepad frame. The
 * caller then runs the cartridge's own curve unchanged.
 */
/**
 * The linear look channel AS PUBLISHED, validity included, for the RECORDER.
 * Same argument as sl_move_channels_peek above: a withheld frame is data.
 */
void sl_mouse_look_peek(int *active, float *yaw_deg, float *pitch_deg)
{
    if (active)    *active    = g_ml_valid;
    if (yaw_deg)   *yaw_deg   = g_ml_yaw_deg;
    if (pitch_deg) *pitch_deg = g_ml_pitch_deg;
}

int sl_mouse_look_get(float *yaw_deg, float *pitch_deg)
{
    if (!g_ml_valid)
        return 0;
    if (yaw_deg)   *yaw_deg   = g_ml_yaw_deg;
    if (pitch_deg) *pitch_deg = g_ml_pitch_deg;
    return 1;
}

#endif /* !__sgi */
