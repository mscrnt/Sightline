/**
 * Native-only byte-swap for the music sequence table (T6).
 *
 * The table is big-endian ROM data that romCopy delivers verbatim into native
 * structs. Raw bytes stay raw during transfer; normalisation happens ONCE at
 * the semantic load boundary, which is the project convention and the reason
 * this lives beside sl_swap_setup.c and sl_swap_model.c rather than being
 * scattered through music.c.
 *
 * Layout authority is the notes, "Music and Sound Effects/Music table
 * format.txt": a big-endian count at the start, entries beginning at +4, each
 * entry a 32-bit offset followed by two 16-bit lengths. That is exactly
 * RareALSeqData, and the documented count is 63.
 *
 * The surface is the count AND every entry. A count-only fix would correct the
 * heap symptom and leave each entry's address and both lengths silently
 * swapped - sl_swap_seq_mode() exists to demonstrate precisely that.
 *
 * The ALBankFile path (bnkf.c) gets its own pass, at the bottom of this file -
 * derived separately, because "similar field names" is not dataflow.
 */
#ifndef __sgi

#include <ultra64.h>
#include <bondgame.h>
#include "music.h"

static u32 sl_ps32(u32 v)
{
    return (v >> 24) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | (v << 24);
}
#define SW32(f) (*(u32 *) &(f) = sl_ps32(*(u32 *) &(f)))
#define SW16(f) (*(u16 *) &(f) = (u16) ((*(u16 *) &(f) >> 8) | (*(u16 *) &(f) << 8)))

/* 0 = raw (reproduce the original failure), 1 = count only (negative control:
 * heap recovers while entries stay wrong), 2 = full normalisation (default). */
int sl_swap_seq_mode(void)
{
    const char *e = getenv("SL_SEQ_MODE");
    return e ? atoi(e) : 2;
}

/* PURE READ. Returns the count in host order from the raw header WITHOUT
 * mutating it, so the temporary 16-byte copy used only for sizing is never
 * normalised and then normalised again by the full-table pass. */
u16 sl_swap_seq_count(const void *rawhdr)
{
    const u8 *b = (const u8 *) rawhdr;
    if (sl_swap_seq_mode() == 0) return (u16) ((b[1] << 8) | b[0]);  /* raw control */
    return (u16) ((b[0] << 8) | b[1]);
}

/* Normalise the complete header and every entry EXACTLY ONCE, before the
 * relocation function or any other consumer runs. musicSeqFileNew stays
 * relocation-only. */
void sl_swap_seqtable(RareALSeqBankFile *f)
{
    s32 i, n, mode = sl_swap_seq_mode();
    if (mode == 0) return;                    /* raw: leave everything swapped */
    SW16(f->seqCount);
    if (mode == 1) return;                    /* count only: entries stay wrong */
    n = (s32) f->seqCount;
    for (i = 0; i < n; i++) {
        SW32(f->seqArray[i].address);
        SW16(f->seqArray[i].uncompressed_len);
        SW16(f->seqArray[i].len);
    }
}

/* ---- the compressed-sequence HEADER -----------------------------------
 * A second boundary in the same subsystem, and a separate one: the sequence
 * TABLE says where a sequence lives; this is the header INSIDE the decompressed
 * sequence itself.
 *
 * Layout is settled from two independent sources that agree field for field.
 * include/PR/libaudio.h:677 declares
 *     typedef struct { u32 trackOffset[16]; u32 division; } ALCMidiHdr;
 * which is 17 words = 0x44 bytes, and the notes ("Music and Sound Effects/
 * Music table format.txt") describe the same container from the data side: a
 * 32-bit offset to the music data "usually 0x44" - that is the header size -
 * then a table of longs, and "always 0x180" in the word immediately before the
 * data, which is exactly `division` at +0x40. The note explicitly does NOT
 * describe the event body after 0x44, and that negative is preserved: the body
 * is byte-oriented and is NOT touched here.
 *
 * THE CHOKE POINT is after decompressdata and before alCSeqNew - the moment the
 * buffer stops being transported bytes and becomes a parsed structure. Every
 * consumer is downstream of it: a tree-wide search for trackOffset and division
 * finds exactly four read sites, all in cseq.c (47, 60, 249, 254) and all via
 * seq->base->. The seq.c hits are ALSeq, a different structure built by explicit
 * read16 byte reads - similar field names, not the same dataflow. Nothing
 * downstream swaps, so this cannot double-swap.
 *
 * Mode 1 exists to prove that "the crash stopped" is not acceptance: it
 * normalises the offsets and deliberately leaves `division` reversed, so the
 * wild pointer disappears while the tempo basis stays wrong.
 */
int sl_swap_seqhdr_mode(void)
{
    const char *e = getenv("SL_SEQHDR_MODE");
    return e ? atoi(e) : 2;
}

void sl_swap_seqheader(void *buf)
{
    u32 *w = (u32 *) buf;
    int i, mode = sl_swap_seqhdr_mode();
    if (mode == 0) return;                    /* raw: reproduce the wild pointer */
    for (i = 0; i < 16; i++) SW32(w[i]);      /* trackOffset[16] */
    if (mode == 1) return;                    /* partial: division left reversed */
    SW32(w[16]);                              /* division */
}

/* ---- the ALBankFile (.ctl) load boundary ------------------------------
 *
 * WHY THIS EXISTS. alBnkfNew (src/libultra/audio/bnkf.c:47) is a pure
 * RELOCATION pass, exactly like musicSeqFileNew: it adds a base to offsets to
 * turn them into pointers and recurses. It reads bankCount, instCount,
 * soundCount, type and every offset as native words. The .ctl segments are
 * big-endian ROM bytes that romCopy delivers verbatim (music.c:668,678), so on
 * this build every one of those is reversed. Measured symptom before the fix:
 * SIGSEGV in __initFromBank (seqplayer.c:1096), whose loop
 * `for (i = 0; !inst; i++) inst = b->instArray[i];` walks off the end of a bank
 * whose instArray entries are all wild.
 *
 * LAYOUT AUTHORITY. docs/doc-routing.json records "bank field layouts" as a
 * not_covered entry - the notes do not describe them - and names the decomp.
 * So: include/PR/libaudio.h:171-267 for the ten structures, and bnkf.c for
 * which fields are offsets rather than values.
 *
 * WHICH FIELDS. Not "every word in the region": this is a structure graph, not
 * a uniformly word-shaped region, and TWO of its arrays are consumed by the
 * RSP rather than the CPU. sl_acmd.c reads DRAM big-endian
 * (`(p[j] << 8) | p[j+1]`, sl_acmd.c:627), so anything the microcode reads must
 * STAY big-endian. Taken from the consumers, not from the field names:
 *
 *   native (CPU reads the value)
 *     ALBankFile revision, bankCount, bankArray[]        bnkf.c:56,62-63
 *     ALBank     instCount, sampleRate, percussion, instArray[]   bnkf.c:70-86
 *     ALInstrument bendRange, soundCount, soundArray[]   bnkf.c:97-101
 *     ALSound    envelope, keyMap, wavetable             bnkf.c:114-117
 *     ALEnvelope attackTime, decayTime, releaseTime      seqplayer.c timings
 *     ALWaveTable base, len, and the union's loop/book   bnkf.c:127-141
 *     ALADPCMBook order, npredictors                     load.c:380-381
 *     ALADPCMloop start, end, count                      load.c:383-385
 *     ALRawLoop   start, end, count                      load.c:396-398
 *
 *   BIG-ENDIAN, deliberately untouched (the RSP reads these bytes)
 *     ALADPCMBook book[]    address handed to the microcode  load.c:69
 *     ALADPCMloop state     alCopy'd to lstate, whose address
 *                           goes to aSetLoop                 load.c:386,461
 *     the .tbl sample data ALWaveTable.base points at - a different segment,
 *     not part of this file, and not walked here.
 *
 *   nothing to swap: ALKeyMap is six u8/s8; ALInstrument's first twelve fields
 *   and ALSound's samplePan/sampleVolume/flags are u8.
 *
 * EXACTLY ONCE PER NODE. bnkf.c dedups with each node's `flags` byte because
 * instruments, sounds and wavetables ARE shared between parents - and a node
 * swapped twice is byte-reversed again, silently. This pass keeps its own
 * visited set rather than borrowing `flags`, which bnkf.c needs to still be
 * zero when it runs.
 */
static void *sl_bank_seen[4096];
static int sl_bank_seen_n;
/* every offset in the file is relative to the file base (bnkf.c:49), so the
 * walk needs it to reach a child while the offsets are still offsets */
static s32 sl_bank_base;

static int sl_bank_visit(void *p)
{
    int i;
    for (i = 0; i < sl_bank_seen_n; i++)
        if (sl_bank_seen[i] == p) return 0;         /* already normalised */
    if (sl_bank_seen_n == 4096) {
        extern void sl_fatalf(const char *fmt, unsigned v);
        sl_fatalf("sightline native: bank swap visited set full (%u)\n", 4096);
    }
    sl_bank_seen[sl_bank_seen_n++] = p;
    return 1;
}

/* 0 = raw (reproduce the __initFromBank fault), 1 = file header and bank
 * headers only (negative control: the top of the graph is right and every
 * instrument below it is still reversed), 2 = full graph (default). */
int sl_swap_bank_mode(void)
{
    const char *e = getenv("SL_BANK_MODE");
    return e ? atoi(e) : 2;
}

static void sl_swap_wavetable(ALWaveTable *w)
{
    if (!sl_bank_visit(w)) return;
    SW32(w->base);                                  /* offset into the .tbl */
    SW32(w->len);
    /* type and flags are u8 - readable either way, and read here to pick the
     * union arm exactly as bnkf.c:132-141 does. */
    if (w->type == AL_ADPCM_WAVE) {
        SW32(w->waveInfo.adpcmWave.book);
        SW32(w->waveInfo.adpcmWave.loop);
        if (w->waveInfo.adpcmWave.book) {
            ALADPCMBook *b = (ALADPCMBook *) ((u8 *) w->waveInfo.adpcmWave.book
                                              + sl_bank_base);
            if (sl_bank_visit(b)) { SW32(b->order); SW32(b->npredictors); }
            /* b->book[] stays big-endian: load.c:69 hands its address to the
             * microcode, which reads DRAM big-endian. */
        }
        if (w->waveInfo.adpcmWave.loop) {
            ALADPCMloop *l = (ALADPCMloop *) ((u8 *) w->waveInfo.adpcmWave.loop
                                              + sl_bank_base);
            if (sl_bank_visit(l)) {
                SW32(l->start); SW32(l->end); SW32(l->count);
                /* l->state stays big-endian: alCopy'd byte-for-byte to lstate
                 * (load.c:386) whose address goes to aSetLoop (load.c:461). */
            }
        }
    } else if (w->type == AL_RAW16_WAVE) {
        SW32(w->waveInfo.rawWave.loop);
        if (w->waveInfo.rawWave.loop) {
            ALRawLoop *l = (ALRawLoop *) ((u8 *) w->waveInfo.rawWave.loop
                                          + sl_bank_base);
            if (sl_bank_visit(l)) { SW32(l->start); SW32(l->end); SW32(l->count); }
        }
    } else {
        extern void sl_fatalf(const char *fmt, unsigned v);
        sl_fatalf("sightline native: bank wavetable type %u is neither "
                  "AL_ADPCM_WAVE nor AL_RAW16_WAVE\n", (unsigned) w->type);
    }
}

static void sl_swap_sound(ALSound *s)
{
    if (!sl_bank_visit(s)) return;
    SW32(s->envelope);
    SW32(s->keyMap);
    SW32(s->wavetable);
    if (s->envelope) {
        ALEnvelope *e = (ALEnvelope *) ((u8 *) s->envelope + sl_bank_base);
        if (sl_bank_visit(e)) {
            SW32(e->attackTime); SW32(e->decayTime); SW32(e->releaseTime);
#ifdef SL_ALHEAP_TRACE
            /* Self-check on THIS pass: ALMicroTime is microseconds, so a
             * correctly normalised release is order 1e3..1e7. A byte-reversed
             * one is astronomically large - and an over-long release is
             * exactly the "voices sustain far too long" signature measured
             * against the cartridge, so the swap must be able to exonerate or
             * convict itself rather than be assumed right. */
            { extern void sl_env_probe(int a, int d, int r, int av, int dv);
              sl_env_probe((int) e->attackTime, (int) e->decayTime,
                           (int) e->releaseTime, (int) e->attackVolume,
                           (int) e->decayVolume); }
#endif
        }
    }
    /* ALKeyMap is six u8/s8 fields - nothing to normalise, and visiting it
     * would only cost a slot. */
    if (s->wavetable)
        sl_swap_wavetable((ALWaveTable *) ((u8 *) s->wavetable + sl_bank_base));
}

static void sl_swap_instrument(ALInstrument *inst)
{
    s32 i, n;
    if (!sl_bank_visit(inst)) return;
    SW16(inst->bendRange);
    SW16(inst->soundCount);
    n = (s32) inst->soundCount;
    for (i = 0; i < n; i++) {
        SW32(inst->soundArray[i]);
        if (inst->soundArray[i])
            sl_swap_sound((ALSound *) ((u8 *) inst->soundArray[i] + sl_bank_base));
    }
}

static void sl_swap_bank(ALBank *bank, int mode)
{
    s32 i, n;
    if (!sl_bank_visit(bank)) return;
    SW16(bank->instCount);
    SW32(bank->sampleRate);
    SW32(bank->percussion);
    n = (s32) bank->instCount;
    for (i = 0; i < n; i++)
        SW32(bank->instArray[i]);
    if (mode == 1) return;              /* headers only: instruments left wrong */
    if (bank->percussion)
        sl_swap_instrument((ALInstrument *) ((u8 *) bank->percussion + sl_bank_base));
    for (i = 0; i < n; i++)
        if (bank->instArray[i])
            sl_swap_instrument((ALInstrument *) ((u8 *) bank->instArray[i]
                                                 + sl_bank_base));
}

/* THE CHOKE POINT: after romCopy has delivered the raw .ctl and BEFORE
 * alBnkfNew relocates it. Offsets are still offsets here, which is what makes
 * the graph walkable; afterwards they are pointers and the counts that bound
 * the walk have already been misread. */
void sl_swap_bankfile(ALBankFile *file)
{
    s32 i, n, mode = sl_swap_bank_mode();
    extern void sl_fatalf(const char *fmt, unsigned v);

    if (mode == 0) return;                          /* raw control */
    sl_bank_seen_n = 0;
    sl_bank_base = (s32) file;
    if (!sl_bank_visit(file)) return;

    SW16(file->revision);
    SW16(file->bankCount);
    /* AL_BANK_VERSION is 'B1' = 0x4231. The file says what it is, so check it
     * rather than trusting that the swap was applied at the right place: a raw
     * read gives 0x3142 here, which is the failure this pass exists to remove. */
    if (file->revision != AL_BANK_VERSION)
        sl_fatalf("sightline native: bank file revision %#x is not "
                  "AL_BANK_VERSION 0x4231 after normalisation\n",
                  (unsigned) (u16) file->revision);
    n = (s32) file->bankCount;
    for (i = 0; i < n; i++)
        SW32(file->bankArray[i]);
    for (i = 0; i < n; i++)
        if (file->bankArray[i])
            sl_swap_bank((ALBank *) ((u8 *) file->bankArray[i] + sl_bank_base), mode);
}

#endif
