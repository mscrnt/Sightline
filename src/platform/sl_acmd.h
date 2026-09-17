/**
 * Native ACMD interpreter - stages 1-3 (output plumbing, mix, FX).
 *
 * This is the audio counterpart of src/gfx/sl_gfx_dl.c: it INTERPRETS the
 * command list the game's own libultra synthesiser builds. It does not
 * execute, translate or emulate the aspMain RSP microcode binary, and it
 * never replaces the synthesis stack above it - alAudioFrame still builds
 * the list exactly as Rare's code does.
 *
 * The seam it fills is src/audi.c:549-561, where the finished list would be
 * handed to the RSP by osSendMesg. Everything before that line is untouched.
 *
 * src/game/ must never include this, exactly as for sl_gfx.h and sl_audio.h.
 *
 * SCOPE, and it is deliberate: only the opcodes the census measured in the
 * zero-voice frames are implemented. Voice synthesis (ADPCM decode, RESAMPLE,
 * ENVMIXER, SETVOL, SETLOOP) is NOT here. Calling a command outside the
 * implemented set is an error the caller can see, never a silent no-op -
 * a mixer that quietly skips what it does not understand produces plausible
 * noise, which is the one outcome indistinguishable from success.
 *
 * AUTHORITY. The corpus does not document the audio microcode or the ACMD
 * command set - see the "audio microcode / ACMD command set" not_covered
 * entry in docs/doc-routing.json. Field ENCODINGS come from include/PR/abi.h
 * (the a* emit macros, :272-402) and are therefore sourced. Execution
 * SEMANTICS are not documented anywhere and are marked INFERRED at each site.
 */
#ifndef SL_ACMD_H
#define SL_ACMD_H

#ifdef __sgi
#error "sl_acmd.h is native-only; guard the include with #ifndef __sgi"
#endif

#include <stdint.h>

/* The RSP has 4KB of DMEM. The census measured every DMEM operand of the
 * implemented opcodes inside 0x0000-0x0800 with counts up to 0x140, so 4KB is
 * headroom rather than a guess - and sl_acmd_stats reports the high-water
 * mark so the claim stays measured rather than remembered. */
#define SL_DMEM_SIZE 0x1000

/* Buffer base. MEASURED, not assumed: every handler that takes a raw DMEM
 * address operand adds 0x5c0 to it before use -
 *   SETBUFF    blob 0x26c  addi at,k0,1472 / addi v0,v0,1472 / addi v1,t9,1472
 *   CLEARBUFF  blob 0x164  addi a0,zero,1472 ; add v0,v0,a0
 *   DMEMMOVE   blob 0x398  addi v0,v0,1472 / addi v1,v1,1472
 *   INTERLEAVE blob 0x31c  addi v1,v1,1472 / addi v0,v0,1472
 * so the audio buffers live at 0x5c0.. and the low DMEM holds the microcode's
 * own data. Applying it uniformly used to be unobservable - a constant shift
 * cancels when every address shifts together - but ADPCM's codebook base is an
 * ABSOLUTE 0x4C0, below 0x5c0, so the two coordinate systems must now agree. */
#ifndef SL_DMEM_BUFBASE
#define SL_DMEM_BUFBASE 0x5c0
#endif

/* Codebook base for ADPCM, absolute. LOADADPCM blob 0x22c: addi at,zero,1216.
 * ADPCM blob 0x490: addi t7,zero,1216. Both 0x4C0, neither offset by 0x5c0. */
#define SL_ADPCM_BOOK   0x4C0

/* The audio state block. MEASURED: every handler addresses its fields off t8,
 * which boot sets to 864 (`addi t8,zero,864`, blob 0x000). It is REAL DMEM,
 * not a set of named variables, and that matters: SETLOOP stores a 32-bit word
 * at +0x10 (blob 0x3e4) while SETVOL(A_RATE|A_LEFT) stores halfwords at +0x10
 * and +0x12 (blob 0x2ec/0x2f0). They are the same storage and CLOBBER each
 * other on hardware. Modelling the block as separate C fields would make that
 * aliasing structurally impossible to reproduce, so the fields live in dmem.
 *
 *   +0x00 in    +0x02 out   +0x04 count   (normal SETBUFF; count NOT offset)
 *   +0x06 vol L +0x08 vol R                (SETVOL A_VOL)
 *   +0x0A/+0x0C/+0x0E                      (A_AUX SETBUFF)
 *   +0x10/+0x12/+0x14  rate L  == SETLOOP's 32-bit loop pointer at +0x10
 *   +0x16/+0x18/+0x1A  rate R                (SETVOL A_RATE)
 *   +0x1C/+0x1E                             (SETVOL A_AUX)
 */
#define SL_ACMD_STATE   0x360

/* Segment table. The census measured a single `aSegment(0, 0)` per sub-frame
 * (segment 0, base 0 - identity), so no segment machinery is exercised. It is
 * modelled anyway because it costs 16 words, but a NON-ZERO base is reported
 * as unimplemented rather than silently applied. */
#define SL_ACMD_NSEG 16

enum {
    SL_ACMD_OK = 0,
    SL_ACMD_ERR_OPCODE,      /* opcode outside the implemented set */
    SL_ACMD_ERR_DMEM,        /* DMEM access out of range */
    SL_ACMD_ERR_DRAM,        /* physical address outside the mapped window */
    SL_ACMD_ERR_SEGMENT,     /* a non-identity segment base was requested */
    SL_ACMD_ERR_UNGROUNDED   /* a path no measurement grounds was reached */
};

typedef struct {
    /* DMEM, held in HARDWARE byte order (big-endian s16 samples). Keeping it
     * that way makes LOADBUFF/SAVEBUFF pure memcpy, exactly as the DMA is,
     * and confines byte order to the arithmetic opcodes where it belongs. */
    uint8_t  dmem[SL_DMEM_SIZE];

    /* The mapped DRAM window. Physical, because the census measured every one
     * of 107657 addr fields below 8MB with ZERO in KSEG0 - the RSP is handed
     * physical addresses. Translation is therefore a subtraction, and there is
     * no virtual-address machinery here on purpose. */
    uint8_t *dram;
    uint32_t dram_lo;
    uint32_t dram_size;

    uint32_t seg[SL_ACMD_NSEG];

    /* The aSetBuffer / aSetVolume / aSetLoop fields all live in dmem at
     * SL_ACMD_STATE - see the layout note above. Nothing is cached here,
     * because a cached copy is exactly what would hide the aliasing between
     * SETLOOP's 32-bit store at +0x10 and SETVOL(A_RATE|A_LEFT)'s halfwords
     * at +0x10 and +0x12. Use sl_acmd_sget/sset to read or prime them. */
    uint8_t  setbuff_flags;

    /* Coefficient table loaded by LOADADPCM. In the zero-voice frames the
     * census proved ALL 25 LOADADPCM per frame are immediately followed by
     * POLEF (25/25), so here it is a pole-filter coefficient table, NOT a
     * voice codebook. That distinction is measured, not assumed. */
    int16_t  coef[16];
    int      coef_len;

    /* measurement */
    uint32_t dmem_high_water;
    uint32_t n_cmds;
    uint32_t n_by_op[16];
    int      err_op;         /* opcode that failed, when err != OK */
    uint32_t err_index;      /* command index that failed */
} sl_acmd_state;

/** Reset everything except the DRAM mapping. */
void sl_acmd_init(sl_acmd_state *s, uint8_t *dram, uint32_t lo, uint32_t size);

/** Execute n commands given as host-order (w0,w1) pairs.
 *
 * Command DECODING is the caller's business up to this point: words arrive
 * already in host order, so a byte-order mistake in the transport cannot be
 * confused with a semantics mistake in here. Returns SL_ACMD_OK or an
 * SL_ACMD_ERR_*; on error, err_op/err_index localise it. */
int sl_acmd_exec(sl_acmd_state *s, const uint32_t *words, uint32_t n);

/* Audio-state-block accessors, for harnesses that need to prime or inspect it.
 * `off` is a byte offset from SL_ACMD_STATE. Halfwords are big-endian, as the
 * RSP's `sh`/`lhu` leave them. */
/* UNGROUNDED-PATH COUNTERS. Each names a branch the measured capture never
 * exercised, or a byte range the cartridge does not settle. They are part of
 * the interface, not debug scaffolding: a nonzero value means production has
 * relied on something no measurement grounds, and the caller should say so
 * rather than let it pass silently. */
extern unsigned long sl_acmd_ungrounded_neg;     /* low-result negative overflow */
extern unsigned long sl_acmd_ungrounded_rsinit;  /* RESAMPLE A_INIT scratch path */
extern unsigned long sl_acmd_ungrounded_state;   /* resampler state +0x10..11    */
/* The ADPCM decode's 48-bit accumulator read as VSAR HIGH:MID - i.e. the low
 * 32 bits taken signed. Of the six decode controls, five move cartridge-visible
 * bytes; this is the one whose ELIGIBILITY over the whole 435-task capture is
 * ZERO, because the accumulator never leaves int32 range there and wrapping is
 * indistinguishable from plain arithmetic. The wrap is kept because it is what
 * the microcode implies, but nothing measures it, so it belongs here. Proven
 * live rather than assumed: at a 2^20 threshold this reads 158404 on the same
 * capture and 0 at the real one, byte-exactness unchanged either way. */
extern unsigned long sl_acmd_ungrounded_adwrap;

/* Same category, same reason. A COUNTER HIT is not a COMMAND and neither is a
 * TASK, so the three quantities are separate: the state and rsinit counters
 * above count individual writebacks, and these count the distinct commands
 * those writebacks occurred in. Units are stated at the increment sites. */
extern unsigned long sl_acmd_cmds_state;         /* commands with a state hit    */
extern unsigned long sl_acmd_cmds_rsinit;        /* commands with an rsinit hit  */
/* The same attribution keyed on the TASK-LOCAL command index, which can alias
 * across task boundaries. Present so the attribution key is measured rather
 * than argued: see the note at the definitions. */
extern unsigned long sl_acmd_cmds_state_local;
extern unsigned long sl_acmd_cmds_rsinit_local;

/* Usage of the decode path. THE RATIONALE HERE CHANGED ON 2026-08-31 and the
 * old one would now mislead: SL_ACMD_ADPCM defaulted to 0, so a nonzero value
 * used to mean unaccepted arithmetic had produced audio. The decode is now
 * grounded against the cartridge - 435/435 tasks byte-exact, challenged by five
 * non-vacuous controls - and defaults to 1, so a nonzero value here is now
 * ORDINARY and is a measure of how much decoding a run did, not a warning.
 * The warning moved to sl_acmd_ungrounded_adwrap above, which is the only part
 * of the decode no measurement settles. */
extern unsigned long sl_acmd_adpcm_cmds;
extern unsigned long sl_acmd_adpcm_samples;   /* 16 per COMPLETED decode frame */
extern unsigned long sl_acmd_adpcm_frames;    /* decode-loop iterations completed */

uint16_t sl_acmd_sget(const sl_acmd_state *s, uint32_t off);
void     sl_acmd_sset(sl_acmd_state *s, uint32_t off, uint32_t v);
uint32_t sl_acmd_sget32(const sl_acmd_state *s, uint32_t off);
void     sl_acmd_sset32(sl_acmd_state *s, uint32_t off, uint32_t v);

const char *sl_acmd_errstr(int err);
const char *sl_acmd_opname(int op);

#endif /* SL_ACMD_H */
