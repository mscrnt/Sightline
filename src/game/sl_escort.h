#ifndef SL_ESCORT_H
#define SL_ESCORT_H

/* Sightline: escort NPCs immune - AI OBSERVATION runs only.
 *
 * Natalya dying ends Control long before the level can be walked for AI
 * reference; the same problem TOUGH= solves for Bond, solved the same way.
 * Uses the game's OWN CHRFLAG_INVINCIBLE, which both damage paths in
 * chraction.c already honour (one flinches and returns, one returns before
 * applying damage), so nothing new is invented.
 *
 * Two things have to happen, and doing only the first is why the earlier
 * attempts appeared to work and did not:
 *
 *   1. SET the flag at BOTH spawn paths - the setup-guard constructor and
 *      chrSpawnAtCoord (0x7F03415C), which is how scripted characters like
 *      Natalya arrive.
 *
 *   2. KEEP it.  Rare's own AI list m_RunToBondPersistent (chraidata.c) -
 *      the list Natalya runs in Control - reaches
 *          IFMyNumArghsLessThan(6, lblNext)
 *          UnsetMychrflags(CHRFLAG_INVINCIBLE)
 *      so after six registered hits the script deliberately strips the
 *      protection back off.  numarghs is incremented BEFORE the invincible
 *      check in chraction.c, so hits count even while they are doing no
 *      damage: six shots from anyone, Bond included, and she is mortal
 *      again.  SL_ESCORT_KEEP masks the bit out of whatever the AI asks to
 *      clear rather than fighting it a frame later.
 *
 * Compiled out entirely for the matching build unless explicitly requested,
 * so Rare's behaviour is what ships.
 */

#if defined(SL_ALLY_INVINCIBLE) || !defined(__sgi)

#ifdef SL_ALLY_INVINCIBLE
#define SL_ESCORT_ENABLED() 1
#else
extern long sl_env_s32(const char *name, long dflt);
#define SL_ESCORT_ENABLED() (sl_env_s32("SL_ALLY_INVINCIBLE", 0) != 0)
#endif

#define SL_ESCORT_BODY(b) \
    ((b) == BODY_Natalya_Skirt || (b) == BODY_Natalya_Jungle_Fatigues)

#define SL_ESCORT_INVINCIBLE(c, b)                                   \
    do                                                               \
    {                                                                \
        if ((c) != NULL && SL_ESCORT_ENABLED() && SL_ESCORT_BODY(b)) \
        {                                                            \
            (c)->chrflags |= CHRFLAG_INVINCIBLE;                     \
        }                                                            \
    } while (0)

/* Drops CHRFLAG_INVINCIBLE from a flag word the AI is about to clear, so that
 * one bit survives.  Deliberately a plain statement rather than an expression:
 * as a ternary folded into the `chrflags &= ~(...)` compound assignment it
 * segfaulted IDO's front end (/usr/lib/cfe, signal 11). */
#define SL_ESCORT_KEEP(c, flags)                                            \
    do                                                                      \
    {                                                                       \
        if ((c) != NULL && SL_ESCORT_ENABLED()                              \
            && SL_ESCORT_BODY((c)->bodynum))                                \
        {                                                                   \
            (flags) = (CHRFLAG) ((flags) & ~CHRFLAG_INVINCIBLE);            \
        }                                                                   \
    } while (0)

#else

#define SL_ESCORT_INVINCIBLE(c, b) ((void) 0)
#define SL_ESCORT_KEEP(c, flags)   ((void) 0)

#endif

#endif /* SL_ESCORT_H */
