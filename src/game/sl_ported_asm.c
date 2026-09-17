/**
 * Native ports of hand-written MIPS assembly (T3/T4).
 *
 * The native build cannot assemble the .s files, so their symbols would
 * otherwise become harvested stubs - fatal for data symbols (writes land in
 * .text) and silently wrong for the RNGs that trace parity depends on.
 * Each port here is a bit-exact translation of the MIPS, kept next to the
 * instruction-level notes.  Compiles away under IDO (__sgi), which uses the
 * original assembly.
 */
#ifndef __sgi

#include "sl_types.h"

/*
 * src/random.s + src/game/chrObjRandom.s - the same 64-bit LFSR-style
 * generator over two independent seeds.  MIPS (dsll32/dsrl32 pairs):
 *   a2 = ((seed & 1) << 32) | ((seed >> 1) & 0xFFFFFFFF)
 *   a2 ^= (seed << 44) >> 32
 *   seed' = ((a2 >> 20) & 0xFFF) ^ a2
 *   return low 32 bits (v0 sign-extends; callers take u32)
 *
 * The (seed >> 1) term keeps 32 bits: dsll a1,seed,31 preserves seed bits
 * 0..32, so the dsrl32 lands seed[32] at bit 31.  An earlier port masked
 * with 0x7FFFFFFF, silently dropping seed bit 32 - correct only for states
 * with bit 32 clear, which is how it survived a spot check.  Verified
 * against emulator seed chains across dozens of live frames.
 */
u64 g_randomSeed       = 0xAB8D9F7781280783ULL;
u64 g_chrObjRandomSeed = 0xAB8D9F7781280783ULL;

static u64 sl_rng_step(u64 seed)
{
    u64 a2;

    a2 = ((seed & 1) << 32) | ((seed >> 1) & 0xFFFFFFFF);
    a2 ^= (seed << 44) >> 32;
    return ((a2 >> 20) & 0xFFF) ^ a2;
}

u32 randomGetNext(void)
{
    g_randomSeed = sl_rng_step(g_randomSeed);
    return (u32) g_randomSeed;
}

/* daddiu on the sign-extended 32-bit argument */
void randomSetSeed(u32 param_1)
{
    g_randomSeed = (u64) ((s64) (s32) param_1 + 1);
}

u32 randomGetNextFrom(u64 *param_1)
{
    *param_1 = sl_rng_step(*param_1);
    return (u32) *param_1;
}

u32 chrObjRandomGetNext(void)
{
    g_chrObjRandomSeed = sl_rng_step(g_chrObjRandomSeed);
    return (u32) g_chrObjRandomSeed;
}

void chrObjRandomSetSeed(u32 param_1)
{
    g_chrObjRandomSeed = (u64) ((s64) (s32) param_1 + 1);
}

/*
 * src/game/math_sincos.s - the GAME'S OWN sinf/cosf, entirely single
 * precision.  These are what matrixmath and every other game caller bind
 * to on hardware; the libultra gu sinf/cosf carry weak aliases
 * (#pragma weak sinf = __sinf), so when this assembly went missing
 * natively the linker silently substituted the DOUBLE-precision gu
 * versions.  That was the whole "1-ULP frontier": standheight and every
 * rotation matrix off by last bits against both emulator families, which
 * faithfully run this code.
 *
 * Bit-exact translation notes:
 *   - cosf is `f12 += pi/2` (0x3fc90fda - one ULP BELOW the nearest-even
 *     pi/2) falling through into sinf.
 *   - round.w.s rounds to nearest, ties to even: rintf under the default
 *     rounding mode.
 *   - |x| >= 2^28 returns +0.0; |x| < 2^-12 returns x unchanged.
 *   - the polynomial is evaluated coefficient + accumulator, all f32.
 */
static const u32 sl_sc_bits[] = {
    0x3fc90fda, /* pi/2 (cosf offset) */
    0x362edef8, /* c4  2.6057805e-06 */
    0xb94fb7ff, /* c3 -1.9809602e-04 */
    0x3c08876a, /* c2  8.3330665e-03 */
    0xbe2aaaa6, /* c1 -1.666666e-01  */
    0x3ea2f983, /* 1/pi              */
    0x40490fdb, /* pi (hi)           */
    0x330885a3, /* pi (lo)           */
};
#define SL_SC(i) (*(const f32 *) &sl_sc_bits[i])

extern float rintf(float);

float sinf(float x)
{
    u32 xpt = ((*(u32 *) &x) >> 22) & 0x1ff;
    f32 xsq, poly, n;
    s32 ni;

    if (xpt < 0xff) {
        if (xpt < 0xe6)
            return x;
        xsq  = x * x;
        poly = SL_SC(1) * xsq;
        poly = SL_SC(2) + poly;
        poly = poly * xsq;
        poly = SL_SC(3) + poly;
        poly = poly * xsq;
        poly = SL_SC(4) + poly;
        poly = poly * xsq;
        poly = poly * x;
        return poly + x;
    }
    if (xpt >= 0x136)
        return 0.0f;
    n  = rintf(SL_SC(5) * x);
    ni = (s32) n;
    x  = x - SL_SC(6) * n;
    x  = x - SL_SC(7) * n;
    xsq  = x * x;
    poly = SL_SC(1) * xsq;
    poly = SL_SC(2) + poly;
    poly = poly * xsq;
    poly = SL_SC(3) + poly;
    poly = poly * xsq;
    poly = SL_SC(4) + poly;
    poly = poly * xsq;
    poly = poly * x;
    poly = poly + x;
    return (ni & 1) ? -poly : poly;
}

float cosf(float x)
{
    return sinf(x + SL_SC(0));
}

#endif /* !__sgi */
