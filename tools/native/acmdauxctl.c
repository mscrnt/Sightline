/* Control for the A_AUX SETBUFF field split.
 *
 * The handler (blob 0x26c) branches on A_AUX and writes two DISJOINT field
 * groups: normal t8+0/+2/+4, aux t8+0xA/+0xC/+0xE. An aux SETBUFF therefore
 * must NOT disturb the normal in/out/count that a later consumer reads.
 *
 * The 435-task capture contains ZERO instances of a normal-state consumer
 * preceded by an aux SETBUFF, so no recorded frame can score this. The control
 * is therefore constructed, and it is built to DISCRIMINATE: arm B reproduces
 * the old behaviour by sending the same operands as a NORMAL SETBUFF, which is
 * exactly what the interpreter used to do with them. If both arms agreed, the
 * control would be vacuous and would prove nothing.
 *
 * Scored on a CONSUMER (LOADBUFF/MIXER/SAVEBUFF all read count), not on the
 * state fields alone - "the metadata changed" is not evidence of behaviour.
 */
#include "../../src/platform/sl_acmd.h"
#include <stdio.h>
#include <string.h>

#define LO 0x1000u
#define SZ 0x2000u
#define SRC 0x1000u
#define DST 0x1800u

enum { OP_ADPCM=1, OP_CLEAR=2, OP_LOAD=4, OP_SAVE=6, OP_SEG=7,
       OP_SETB=8, OP_MIX=12 };

static uint32_t W[64]; static int NW;
static void cmd(uint32_t w0, uint32_t w1){ W[2*NW]=w0; W[2*NW+1]=w1; NW++; }

/* returns number of DST bytes that changed from the 0xAA fill */
static int run(int aux_flag, sl_acmd_state *s, uint8_t *dram)
{
    int i, ch = 0, rc;
    memset(dram, 0, SZ);
    for (i = 0; i < 0x20; i++) dram[SRC - LO + i] = (uint8_t)(0x10 + i);
    memset(dram + (DST - LO), 0xAA, 0x40);

    NW = 0;
    cmd(OP_SEG  << 24, 0);
    cmd((OP_SETB << 24) | 0x100u, (0x200u << 16) | 0x20u);   /* normal */
    cmd(OP_LOAD << 24, SRC);
    cmd((OP_CLEAR << 24) | 0x200u, 0x20u);
    /* the command under test: aux_flag=8 -> A_AUX, aux_flag=0 -> old behaviour */
    cmd((OP_SETB << 24) | ((uint32_t)aux_flag << 16) | 0x300u,
        (0x400u << 16) | 0x08u);
    cmd((OP_MIX << 24) | 0x7FFFu, (0x100u << 16) | 0x200u);
    cmd(OP_SAVE << 24, DST);

    sl_acmd_init(s, dram, LO, SZ);
    rc = sl_acmd_exec(s, W, NW);
    if (rc != SL_ACMD_OK) { printf("    exec failed: %s\n", sl_acmd_errstr(rc)); return -1; }
    for (i = 0; i < 0x40; i++) if (dram[DST - LO + i] != 0xAA) ch++;
    return ch;
}

/* SETLOOP / SETVOL aliasing, and the flags==0 branch.
 *
 * SETLOOP stores a 32-bit word at t8+0x10 (blob 0x3e4). SETVOL(A_RATE|A_LEFT)
 * stores halfwords at t8+0x10 and t8+0x12 (blob 0x2ec/0x2f0). Same storage.
 * With the state block modelled as named C fields this was structurally
 * impossible to reproduce; with it in real DMEM the clobber falls out.
 *
 * Also scored here: A_RATE|A_RIGHT is flags == 0. That is a REAL combination
 * selecting t8+0x16..0x1A, not "no flags" - so it must leave 0x10 alone.
 */
enum { OP_SETVOL = 9, OP_SETLOOP = 15 };

static int alias_control(void)
{
    static uint8_t dram[SZ];
    sl_acmd_state s;
    uint32_t after_loop, after_ratel, after_rater, reloop;
    int ok = 1;

    sl_acmd_init(&s, dram, LO, SZ);
    NW = 0;
    cmd(OP_SETLOOP << 24, 0x00123456u);
    sl_acmd_exec(&s, W, NW);
    after_loop = sl_acmd_sget32(&s, 0x10);

    /* A_RATE|A_RIGHT is flags == 0 - must NOT disturb 0x10 */
    NW = 0;
    cmd((OP_SETVOL << 24) | 0xBEEFu, (0xCAFEu << 16) | 0xF00Du);
    sl_acmd_exec(&s, W, NW);
    after_rater = sl_acmd_sget32(&s, 0x10);

    /* A_RATE|A_LEFT is flags == 2 - MUST clobber 0x10/0x12 */
    NW = 0;
    cmd((OP_SETVOL << 24) | (2u << 16) | 0x1111u, (0x2222u << 16) | 0x3333u);
    sl_acmd_exec(&s, W, NW);
    after_ratel = sl_acmd_sget32(&s, 0x10);

    /* and SETLOOP clobbers the left rate back again */
    NW = 0;
    cmd(OP_SETLOOP << 24, 0x00ABCDEFu);
    sl_acmd_exec(&s, W, NW);
    reloop = sl_acmd_sget32(&s, 0x10);

    printf("SETLOOP/SETVOL aliasing control\n\n");
    printf("    after SETLOOP 0x123456          : t8+0x10 = %#010x\n", after_loop);
    printf("    after SETVOL A_RATE|A_RIGHT (0) : t8+0x10 = %#010x   rate R at 0x16 = %#06x\n",
           after_rater, sl_acmd_sget(&s, 0x16));
    printf("    after SETVOL A_RATE|A_LEFT  (2) : t8+0x10 = %#010x   (loop address destroyed)\n",
           after_ratel);
    printf("    after SETLOOP 0xABCDEF          : t8+0x10 = %#010x   left rate destroyed, 0x14 = %#06x\n",
           reloop, sl_acmd_sget(&s, 0x14));

    if (after_loop != 0x00123456u) { printf("    FAIL: SETLOOP did not land\n"); ok = 0; }
    if (after_rater != 0x00123456u) { printf("    FAIL: flags==0 disturbed 0x10\n"); ok = 0; }
    if (sl_acmd_sget(&s, 0x16) != 0xBEEFu) { printf("    FAIL: flags==0 did not write 0x16\n"); ok = 0; }
    if (after_ratel != 0x11112222u) { printf("    FAIL: A_RATE|A_LEFT did not clobber\n"); ok = 0; }
    if (reloop != 0x00ABCDEFu) { printf("    FAIL: SETLOOP did not clobber back\n"); ok = 0; }
    printf("\n%s\n\n", ok
        ? "CONTROL FIRES: the two commands share storage in BOTH directions, and\n"
          "  flags==0 is handled as a real selector rather than as absent."
        : "CONTROL DID NOT BEHAVE AS DERIVED - do not trust it.");
    return ok;
}

/* CONSTRUCTED boundary control - NOT cartridge-measured.
 *
 * Under the proven reachable state (carry register zero) the clip instruction
 * reduces to: compute the WRAPPED 16-bit difference of the operands, then
 * select the second operand if that difference read as signed is non-negative,
 * otherwise the first. That is NOT general signed minimum: the two disagree
 * exactly when the subtraction overflows 16 bits.
 *
 * This control exists so evaluator B can never quietly substitute min() for the
 * literal rule. It is constructed from opposite-sign extremes, not taken from
 * any measured command, and proves only that the two rules are distinguishable.
 */
static int clip_rule_control(void)
{
    struct { int16_t vs, vt; } cases[] = {
        { -32768,  32767 }, {  32767, -32768 }, { -32768,      1 },
        {  32767,     -1 }, {    100,    -200 }, {   -200,    100 },
    };
    unsigned i, differ = 0;
    printf("CONSTRUCTED clip-rule control (not cartridge-measured)\n\n");
    printf("      vs      vt   wrapped-diff  literal  naive-min  differ\n");
    for (i = 0; i < sizeof cases / sizeof *cases; i++) {
        int16_t vs = cases[i].vs, vt = cases[i].vt;
        uint16_t d = (uint16_t)((uint16_t)vs - (uint16_t)vt);
        int16_t lit = ((int16_t)d >= 0) ? vt : vs;             /* literal rule  */
        int16_t nai = (vs < vt) ? vs : vt;                     /* naive signed min */
        int diff = (lit != nai);
        differ += diff;
        printf("  %6d  %6d   %#06x     %6d   %6d      %s\n",
               vs, vt, d, lit, nai, diff ? "YES" : "no");
    }
    printf("\n%s\n\n", differ
        ? "CONTROL FIRES: the literal wrapped-subtraction rule and a naive signed\n"
          "  minimum disagree on overflow-capable operands, so min() is NOT a safe\n"
          "  substitution without an operand-domain invariant ruling out overflow."
        : "CONTROL DID NOT FIRE - the two rules agreed everywhere; do not trust it.");
    return differ > 0;
}

int main(void)
{
    static uint8_t dram[SZ];
    sl_acmd_state s;
    int a, b;

    if (!clip_rule_control()) return 1;

    if (!alias_control()) return 1;

    printf("A_AUX SETBUFF field-split control\n\n");

    printf("arm A  aux SETBUFF (A_AUX set) between normal SETBUFF and consumers\n");
    a = run(8, &s, dram);
    printf("    normal in/out/count : %#06x %#06x %#06x\n",
           sl_acmd_sget(&s,0x00), sl_acmd_sget(&s,0x02), sl_acmd_sget(&s,0x04));
    printf("    aux    a/c/e        : %#06x %#06x %#06x\n",
           sl_acmd_sget(&s,0x0A), sl_acmd_sget(&s,0x0C), sl_acmd_sget(&s,0x0E));
    printf("    DST bytes written by the consumer : %d\n\n", a);

    printf("arm B  SAME operands sent as a NORMAL SETBUFF (the old behaviour)\n");
    b = run(0, &s, dram);
    printf("    normal in/out/count : %#06x %#06x %#06x\n",
           sl_acmd_sget(&s,0x00), sl_acmd_sget(&s,0x02), sl_acmd_sget(&s,0x04));
    printf("    DST bytes written by the consumer : %d\n\n", b);

    /* Expected: A preserves count 0x20 and writes 32 bytes; B is clobbered to
     * count 0x08 and writes 8. */
    if (a == 0x20 && b == 0x08 &&
        sl_acmd_sget(&s,0x00) == 0x300u + SL_DMEM_BUFBASE)
        printf("CONTROL FIRES: the arms differ (%d vs %d bytes), so the split is\n"
               "  load-bearing, and arm A preserves the normal fields.\n", a, b);
    else
        printf("CONTROL DID NOT FIRE AS EXPECTED (a=%d b=%d) - do not trust it.\n", a, b);
    return !(a == 0x20 && b == 0x08);
}
