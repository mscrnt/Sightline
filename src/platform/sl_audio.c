/* SDL2 audio device behind the three osAi* entry points.
 *
 * This is a thin transport, deliberately: it takes PCM that something else
 * produced and pushes it at a DAC. It never generates a sample of its own.
 * If the buffers handed to sl_audio_submit are zeros, this file plays silence
 * and sl_audio_report says so - that distinction is the whole point of the
 * statistics below, because a healthy callback count proves nothing.
 *
 * Gating: a device is opened only when a real window is up, or SL_AUDIO=1
 * forces one. Headless trace replay therefore opens NO device, services no
 * callbacks, and cannot perturb the sim.
 */
#ifndef __sgi
#include "sl_audio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include <time.h>

int sl_gfx_active(void);

static SDL_AudioDeviceID g_dev;
static int   g_open;
static int   g_tried;
static unsigned int g_rate = 22050;
static FILE *g_dump;
static int   g_swap;               /* submitted PCM is big-endian */
static unsigned int g_obt_freq, g_obt_chans, g_obt_bps;   /* the DEVICE's format */

/* measurement - see sl_audio_report */
static unsigned long long g_submits;      /* osAiSetNextBuffer calls */
static unsigned long long g_bytes;        /* bytes handed over */
static unsigned long long g_frames;       /* stereo sample frames */
static unsigned long long g_nonzero;      /* samples != 0 */
static unsigned long long g_sumsq;        /* for RMS */
static int   g_peak;
static unsigned long long g_silent_blocks;
static unsigned long long g_blocks;

/* the AI's own deterministic queue - see sl_audio_ai_retrace below */
static unsigned int       g_ai_rate = 22050;
/* DOUBLE BUFFERED, two entries, because that is what the hardware is:
 * include/PR/rcp.h:607 - "The address and length registers are double
 * buffered; that is, they can be written twice before becoming full." */
#define SL_AI_QUEUE 2
static unsigned int       g_ai_q[SL_AI_QUEUE]; /* bytes still owed, per entry */
static int                g_ai_qn;             /* entries occupied */
static unsigned long long g_ai_rem;       /* sub-byte remainder, so none is lost */
/* Counted, not assumed. A refusal means the game handed the DAC a buffer it
 * could not take and dropped it (the game ignores the -1, audi.c:894); an
 * underrun means the DAC ran dry, which audi.c:1002 treats as the error
 * condition "ai out of samples". Both are reported by sl_audio_report. */
unsigned long sl_ai_refused, sl_ai_underruns, sl_ai_submits;

static int sl_audio_wanted(void)
{
    const char *v = getenv("SL_AUDIO");
    if (v != NULL)
        return strcmp(v, "0") != 0;
    /* default: follow the window. No window -> trace replay -> stay silent. */
    return sl_gfx_active();
}

static void sl_audio_open(unsigned int rate)
{
    SDL_AudioSpec want, got;
    const char *d;

    if (g_tried)
        return;
    g_tried = 1;

    if (!sl_audio_wanted()) {
        fprintf(stderr, "sightline audio: disabled (no window, SL_AUDIO unset)\n");
        return;
    }
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "sightline audio: SDL_InitSubSystem failed: %s\n", SDL_GetError());
        return;
    }

    memset(&want, 0, sizeof want);
    want.freq     = (int) rate;
    want.format   = AUDIO_S16SYS;
    want.channels = 2;
    /* The game hands over one video frame of audio at a time (~370 stereo
     * frames at 22050/60). A 1024-frame device buffer is two of those, which
     * is enough to ride out scheduling jitter without adding audible lag. */
    want.samples  = 1024;
    want.callback = NULL;             /* queue-driven: SDL_QueueAudio */

    g_dev = SDL_OpenAudioDevice(NULL, 0, &want, &got, 0);
    if (g_dev == 0) {
        fprintf(stderr, "sightline audio: SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
        return;
    }
    g_open = 1;
    g_rate = (unsigned int) got.freq;
    /* THE OBTAINED FORMAT, not the requested one. Queued BYTES only become a
     * latency once divided by the device's real byte rate, and SDL is free to
     * grant a different rate, channel count or sample width than was asked
     * for. Deriving the divisor from `want` would report a latency the device
     * does not have. */
    g_obt_freq  = (unsigned int) got.freq;
    g_obt_chans = (unsigned int) got.channels;
    g_obt_bps   = (unsigned int) (SDL_AUDIO_BITSIZE(got.format) / 8);
    SDL_PauseAudioDevice(g_dev, 0);
    fprintf(stderr, "sightline audio: device open %u Hz %d ch, %u-frame buffer\n",
            g_rate, got.channels, (unsigned) got.samples);
    (void) d;
}

/* Deliberately independent of whether a DEVICE opened: the dump is the
 * measurement path, and the thing most worth measuring is what the game
 * produces on a box with no working DAC. Tying it to the device meant a
 * headless run silently wrote no file while every counter still moved. */
static void sl_audio_dump_open(void)
{
    static int tried;
    const char *d;

    if (tried)
        return;
    tried = 1;
    if ((d = getenv("SL_AUDIO_DUMP")) == NULL)
        return;
    g_dump = fopen(d, "wb");
    fprintf(stderr, "sightline audio: dumping raw s16 stereo to %s (%s)\n",
            d, g_dump ? "open" : "FAILED");
}

/* THE RATE THE DAC ACTUALLY RUNS AT, computed exactly as libultra computes it
 * (src/libultra/io/aisetfreq.c): the DAC period is a 14-bit divider off the
 * video clock, so the achieved rate is osViClock/dacRate and NOT the rate that
 * was asked for. For GoldenEye's OUTPUT_RATE of 22050 the divider is 2208 and
 * the DAC runs at 22047 Hz - which is also what parallel_n64 reports once the
 * game has configured the AI (docs/doc-routing.json, audio-music-sfx).
 *
 * Two things this fixes, and the second matters more than the first:
 *
 *   - the drain rate was the REQUESTED 22050, so the model drained 0.2 bytes
 *     per retrace faster than the hardware;
 *   - the RETURN value was `g_open ? g_rate : rate`, the rate SDL happened to
 *     grant. The game assigns it to alconf->outputRate and sizes g_FrameSize
 *     from it (audi.c:426-428), so the host's audio device could reach the
 *     simulation and change the emitted command stream. A host that granted
 *     48000 would have produced a different g_FrameSize and a different stream.
 *     Phase 0's rule is that real time and host state never reach the sim; this
 *     was a hole in it. The value is now a pure function of osViClock and the
 *     requested rate, identical with or without a device. */
unsigned int sl_audio_set_frequency(unsigned int rate)
{
    /* osViClock itself is NOT linked natively - build.sh compiles
     * src/libultrare/audio but not src/libultrare/io - so the constant is
     * spelled out with its source rather than declared extern. This build is
     * NTSC (REFRESH_NTSC), and src/libultrare/io/vi.c:11 sets
     * osViClock = VI_NTSC_CLOCK, which include/PR/rcp.h:599 gives as 48681812. */
    const unsigned int vi_clock = 48681812u;     /* VI_NTSC_CLOCK */
    unsigned int req = rate ? rate : 22050;
    unsigned int dac, achieved;

    /* libultra: f = osViClock/frequency + 0.5; dacRate = (int) f; */
    dac = (unsigned int) ((double) vi_clock / (double) req + 0.5);
    if (dac < 132u) dac = 132u;                  /* AI_MIN_DAC_RATE */
    achieved = vi_clock / dac;

    /* THE SUBMITTED PCM IS BIG-ENDIAN, and that is measured on console bytes
     * rather than assumed. The buffer the game hands osAiSetNextBuffer is the
     * audio arena region the RSP wrote, and the cartridge capture's own RDRAM
     * settles its order: read as big-endian s16 the console's output block is
     * a waveform (lag-1 autocorrelation r=+0.99 at audioFrame 301 of
     * /tmp/sl-acmd4/all.bin); read host-order it is full-scale noise
     * (r=+0.18, peak 32768 = the -32768 that a swapped low byte produces).
     * The native arena holds the same bytes - acmdreplay scores 435/435
     * frames byte-exact against that capture - so it is big-endian too.
     *
     * SDL wants AUDIO_S16SYS, i.e. host order, so the swap is REQUIRED on the
     * way to the device. It used to be opt-in behind SL_AUDIO_BE, which meant
     * a correct arena reached the DAC byte-swapped: the loudest possible
     * wrong answer, and one that sounds like a broken decoder rather than
     * like a transport bug. SL_AUDIO_BE=0 is now the control that restores
     * the old behaviour.
     *
     * The measurement counters below are deliberately taken in the SAME order
     * the device sees, so peak/RMS/nonzero describe the audio rather than its
     * transport. */
    { const char *p = getenv("SL_AUDIO_BE"); g_swap = !(p && *p == '0'); }
    g_ai_rate = achieved;
    sl_audio_dump_open();
    sl_audio_open(req);
    return achieved;
}

/* ---- the audio ARENA -----------------------------------------------------
 *
 * DERIVED, not chosen. The ACMD stream carries buffer addresses in its command
 * words, and the acceptance criterion is that those words match the
 * cartridge's. Three measurements settle what that requires:
 *
 *  1. The cartridge sets up the segment machinery and never uses it: all 2175
 *     SEGMENT commands in the facility capture are (segment 0, base 0), so
 *     segaddr is the identity and the operand IS a physical address. There is
 *     no base to absorb a difference.
 *  2. The cartridge's audio heap is at physical 0x002c7ca0, length 0x2e000
 *     (= MUSIC_ALLOCATION_BYTES), read from its own RDRAM at g_musicHeap
 *     (0x80063710, build/u/ge007.u.map:10692). Every RSP-addressed operand in
 *     the capture lies inside that extent.
 *  3. So the operands are 24-bit-clean as a CONSEQUENCE of the heap being
 *     below 4 MB, not as a separate property to engineer.
 *
 * Hence: put the native audio heap at the cartridge's own physical address.
 *
 * IT ALSO FIXES A SECOND DEFECT, and that is why one change covers both.
 * Native audio addresses appeared to come from two disjoint regions (operand
 * top bytes 0xf6 and 0x16). They do not. Every buffer involved is
 * alHeapAlloc'd from this one heap - f->state and f->lstate at
 * libultrare/audio/drvrNew.c:237-238, the codebook inside the bank. The
 * difference is only which macro converts the pointer at emit time: three
 * sites use K0_TO_PHYS (load.c:69, 461, 465), which is ((x)&0x1FFFFFFF)
 * (include/PR/R4300.h:55), while the rest use osVirtualToPhysical, which is
 * the identity natively (sl_ultra_shim.c:672). On the N64 that mask is right -
 * KSEG0 0x802c7ca0 becomes physical 0x002c7ca0. On a host pointer it is
 * destructive: 0xf6bbf294 becomes 0x16bbf294. Once the heap really is at
 * 0x002c7ca0 every pointer into it is already below 0x20000000, so K0_TO_PHYS
 * is the identity on them and the mangling disappears on its own. The two
 * "regions" were always one region.
 *
 * The memory POOL is deliberately left alone. music.c still makes its
 * mempAllocBytesInBank call and simply does not use the result for the heap,
 * so every other allocation keeps the address it had. The trace gate is
 * load-bearing on anything that moves allocations, and this keeps that surface
 * at zero.
 */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/mman.h>
#endif

#define SL_AUDIO_ARENA_BASE 0x002c7ca0u   /* cartridge g_musicHeap, physical */
#define SL_AUDIO_ARENA_STATIC_BYTES 0x2E000u  /* == MUSIC_ALLOCATION_BYTES */
#ifdef _WIN32
#define SL_ARENA_FAILED ((void *) 0)
#else
#define SL_ARENA_FAILED MAP_FAILED
#endif

static int g_arena_live;        /* an arena is in force (below 0x20000000) */
static int g_arena_cartridge;   /* ...and it is at the cartridge's own base,
                                 * so operands equal the cartridge's too */

int sl_audio_arena_live(void) { return g_arena_live; }
int sl_audio_arena_at_cartridge_base(void) { return g_arena_cartridge; }

void *sl_audio_arena(unsigned int len)
{
    static void *arena;
    unsigned long want = SL_AUDIO_ARENA_BASE;
    unsigned long page = want & ~0xFFFUL;
    unsigned long span = ((want - page) + len + 0xFFFUL) & ~0xFFFUL;
    void *m;
#ifndef _WIN32
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#endif

    if (arena != 0)
        return arena;
#ifdef _WIN32
    /* Win32 reserves at 64KB granularity, not page granularity. */
    page = want & ~0xFFFFUL;
    span = ((want - page) + len + 0xFFFUL) & ~0xFFFUL;
    m = VirtualAlloc((void *) page, span, MEM_RESERVE | MEM_COMMIT,
                     PAGE_READWRITE);
    if (m == (void *) page) {
        g_arena_cartridge = 1;
        g_arena_live = 1;
        arena = (void *) (unsigned long) want;
        fprintf(stderr, "sightline audio: arena at %#lx len %#x "
                        "(cartridge g_musicHeap placement)\n", want, len);
        return arena;
    }
    if (m != 0) { VirtualFree(m, 0, MEM_RELEASE); m = 0; }

    /* THE CARTRIDGE BASE IS NOT AVAILABLE ON WINDOWS, and that is a property
     * of the process layout rather than of this code.
     *
     * MEASURED 2026-09-02 inside the game process itself (a bare i686 process
     * does NOT reproduce it, which is why it has to be measured here): at
     * main() entry 0x002c0000 already carries AllocationBase 0x00200000,
     * State MEM_RESERVE, Type MEM_PRIVATE, RegionSize 0x75000, and
     * VirtualAlloc on it returns ERROR_INVALID_ADDRESS (487). It is claimed
     * before main() runs, so there is no ordering fix from inside the program;
     * reserving earlier was tried at main() entry and refused identically.
     *
     * SEPARATE THE TWO THINGS THE CARTRIDGE BASE WAS BUYING, because only one
     * of them is required for audio to exist:
     *
     *   PARITY      operands equal to the ones the cartridge emitted. Needs
     *               exactly 0x002c7ca0. Windows cannot supply it. This is a
     *               comparison convenience, not a runtime requirement.
     *   CORRECTNESS the operands the audio path emits must survive BOTH
     *               truncations they meet on the way to the interpreter, so
     *               that they land in the same address domain as the dram_lo
     *               the interpreter resolves against (sl_ultra_shim.c:256,
     *               dram_lo = the host audio-heap base):
     *
     *                 K0_TO_PHYS, ((u32)(x)&0x1FFFFFFF), applied by the voice
     *                 path (libultrare/audio/load.c:69,461,465) while the rest
     *                 of the path uses the natively-identity
     *                 osVirtualToPhysical.  Identity iff addr < 0x20000000.
     *
     *                 segaddr, s->seg[(w1>>24)&15] + (w1 & 0xFFFFFF)
     *                 (sl_acmd.c:427), applied to every DRAM operand the RSP
     *                 resolves - LOADADPCM among them.  Every segment base the
     *                 census measured is 0, so this is the identity iff the
     *                 operand is 24-BIT CLEAN, i.e. addr < 0x01000000.
     *
     *               The second bound is the tighter one and it is the whole
     *               requirement: an arena entirely below 0x01000000 satisfies
     *               both at once. This is the same property sl_audio.c's own
     *               derivation recorded above - "the operands are 24-bit-clean
     *               as a CONSEQUENCE of the heap being below 4 MB" - restated
     *               as the constraint to solve for rather than an observation
     *               about the cartridge.
     *
     * Correctness is satisfiable on Windows even though parity is not, so the
     * arena is placed at a fixed 24-bit-clean base instead of being abandoned.
     * Abandoning it is what silenced Windows audio: the fallback pool pointer
     * moved above 0x20000000 when B-066 placed the game heap at 0x20000000,
     * K0_TO_PHYS then stripped that bit from every voice-path operand, and
     * phys() rejected each one as below dram_lo - LOADADPCM faulting on
     * command 5 of every task, so no task ever decoded a sample and every
     * submitted block was digital silence.
     *
     * MEASURED, and it is why the bound is 0x01000000 and not 0x20000000: an
     * arena at 0x10000000 clears K0_TO_PHYS but NOT segaddr, which reads the
     * 0x10 top byte as a segment index and resolves 0x10007730 to seg[0] +
     * 0x7730 = 0x7730. That run still faulted at LOADADPCM command 5, with
     * the operand now inside the printed window - the domain error moves
     * rather than disappearing. Only the 24-bit-clean placement removes it.
     *
     * THE ALLOCATOR CANNOT SUPPLY 24-BIT-CLEAN MEMORY ON THIS HOST, measured
     * rather than assumed, and that is what selects the mechanism below.
     * Walking every region under 16 MB at main() entry and again at audio-init
     * time gives the same answer both times: the largest FREE block is 0xf000
     * at 0x00021000, and 0x2e000 is needed. The low 16 MB is the image plus
     * the loader's and CRT's own private reservations, all of them claimed
     * before main() runs. Reserving earlier does not help - it was tried at
     * main() entry and refused identically - and neither does moving the image
     * out of the way: relinking at --image-base=0x30000000 freed 0x400000
     * upward and the process heap had taken the whole of it before main(),
     * leaving the largest free block still 0xf000. A runtime reservation
     * cannot win a race that is already over when the program starts.
     *
     * So the arena is not requested from the allocator at all: it is STATIC
     * STORAGE INSIDE THE IMAGE, which the loader maps before any heap exists
     * and therefore cannot lose the race. It is pinned to .data rather than
     * left in .bss deliberately. With the default 0x00400000 image base .data
     * begins at 0x0057b000 and is a few hundred KB long, so every object in it
     * is 24-bit clean no matter how the objects are ordered; .bss begins at
     * 0x005d5000 but runs 0xe77c54 bytes, past 0x01000000, so a .bss placement
     * would depend on link order for its correctness. The cost is 0x2e000 of
     * zeros on disk, which is the price of the guarantee.
     *
     * The bound is still CHECKED at runtime and still fails loudly, because a
     * future image base or section layout could invalidate the reasoning above
     * and this must not degrade back into silence that has to be rediscovered.
     *
     * The base shares the cartridge base's zero top nibble by necessity -
     * 24-bit-clean and nonzero-top-nibble cannot both hold - which is exactly
     * the property the accepted build ran with, because audio buffers never
     * reach dl_operand and so never meet B-066's test. */
    {
        static unsigned char g_arena_storage[SL_AUDIO_ARENA_STATIC_BYTES]
            __attribute__((section(".data")));
        unsigned long b = (unsigned long) (void *) g_arena_storage;

        span = (len + 0xFFFUL) & ~0xFFFUL;
        if (len <= sizeof g_arena_storage && b + len <= 0x01000000ul) {
            g_arena_live = 1;
            arena = (void *) g_arena_storage;
            fprintf(stderr,
                    "sightline audio: arena at %p len %#x (24-bit-clean static "
                    "placement; the cartridge base %#lx was refused)\n"
                    "sightline audio: address operands will NOT equal the "
                    "cartridge's - audio is correct, operand PARITY is not "
                    "claimed.\n",
                    arena, len, want);
            return arena;
        }
        fprintf(stderr,
                "*** sightline audio: static arena at %#lx len %#x is NOT "
                "24-bit clean (needs end <= %#lx).\n", b, len, 0x01000000ul);
    }
    if (m != 0) { VirtualFree(m, 0, MEM_RELEASE); m = 0; }
#else
#ifdef MAP_FIXED_NOREPLACE
    flags |= MAP_FIXED_NOREPLACE;       /* refuse to clobber an existing map */
#else
    flags |= MAP_FIXED;
#endif
    m = mmap((void *) page, span, PROT_READ | PROT_WRITE, flags, -1, 0);
    if (m != SL_ARENA_FAILED && m == (void *) page) {
        g_arena_cartridge = 1;
        g_arena_live = 1;
        arena = (void *) (unsigned long) want;
        fprintf(stderr, "sightline audio: arena at %#lx len %#x "
                        "(cartridge g_musicHeap placement)\n", want, len);
        return arena;
    }
#endif
    /* Loud, and NOT fatal: this is an environment limit, not an unsupported
     * semantic. The run continues on the pool pointer - but that pointer is
     * not 24-bit clean, so segaddr and K0_TO_PHYS between them mangle every
     * audio operand and the result is silence. Say so rather than leaving it
     * to be rediscovered. */
#ifdef _WIN32
    fprintf(stderr,
            "*** sightline audio: could NOT reserve a 24-bit-clean audio "
            "arena below %#lx.\n"
            "*** Audio operands will be mangled by segaddr/K0_TO_PHYS and "
            "audio will be SILENT.\n", 0x01000000ul);
#else
    if (m != SL_ARENA_FAILED && m != (void *) page)
        munmap(m, span);
    fprintf(stderr,
            "*** sightline audio: could NOT map the audio arena at %#lx.\n"
            "*** Address operands will NOT match the cartridge. Check\n"
            "*** vm.mmap_min_addr (needs < %#lx; it is the only reason\n"
            "*** this fails on a normal system).\n", want, want);
#endif
    return 0;
}

/* ---- the AI's clock ------------------------------------------------------
 *
 * osAiGetLength reports the bytes the DAC has not drained yet, and the audio
 * manager sizes EVERY frame from it:
 *
 *     info->frameSamples = ((g_FrameSize - (osAiGetLength() >> 2))
 *                           + 16 + EXTRA_SAMPLES) & ~0xf;      audi.c
 *
 * with Rare's own comment above it: "this will vary slightly frame to frame,
 * must recalculate every frame". It is how the audio frame stays locked to the
 * video field despite jitter.
 *
 * This used to return a constant 0 with no device open, and 0 does not mean
 * "unknown" - it means the DAC is empty, so the manager asked for its LARGEST
 * frame every single time and the recalculation was vacuous. Measured against
 * the cartridge at the same audio frame, that is where the first
 * address-independent difference in the command stream appeared: a CLEARBUFF
 * count of 0x120 where the cartridge emitted 0xa0.
 *
 * Two things it must NOT be:
 *   - SDL_GetQueuedAudioSize. Real time, host-dependent, and dependent on
 *     whether a device opened at all. The command stream has to be
 *     reproducible - byte-identity of the emitted stream is the acceptance
 *     criterion for this track - so host queue depth cannot reach the sim.
 *     Phase 0's rule that real time never reaches the simulation covers this
 *     exactly.
 *   - a constant. See above.
 *
 * So the AI is modelled off the SAME fixed-step clock as everything else: a
 * FIFO that gains whatever osAiSetNextBuffer hands it and loses exactly one
 * retrace worth of samples per retrace. The drain rate is the game's own
 * arithmetic rather than a fitted number - the DAC consumes `rate` stereo
 * samples a second at 4 bytes each, over the MAYBE_FRAME_RATE (60) retraces a
 * second that amCreateAudioManager itself divides by - i.e. rate*4/60 bytes a
 * retrace, with the remainder accumulated so no fraction is dropped.
 *
 * At 22050 that is 1470 bytes a retrace, so 2940 bytes = 735 stereo samples
 * per audio frame against a g_FrameSize of 736. The one-sample surplus is
 * precisely the drift the varying frameSamples exists to absorb, and the model
 * is self-correcting because of it: the FIFO fills, frameSamples drops, the
 * FIFO drains. It was not tuned to produce that - it falls out of the rate.
 *
 * The SDL device stays a pure downstream consumer and is never consulted here.
 */
void sl_audio_ai_retrace(void)
{
    unsigned long long d;

    g_ai_rem += (unsigned long long) g_ai_rate * 4ULL;
    d = g_ai_rem / 60ULL;
    g_ai_rem -= d * 60ULL;

    /* Drain the HEAD entry first and carry the remainder into the next, which
     * is what a queue of transfers does; the previous model kept one running
     * total and so could not distinguish "one buffer nearly done" from "two
     * buffers outstanding". Running dry is the condition audi.c calls "ai out
     * of samples", so it is counted rather than silently clamped. */
    while (d > 0 && g_ai_qn > 0) {
        unsigned int head = g_ai_q[0];
        if ((unsigned long long) head > d) { g_ai_q[0] = head - (unsigned int) d; d = 0; }
        else {
            int i;
            d -= head;
            for (i = 1; i < g_ai_qn; i++) g_ai_q[i - 1] = g_ai_q[i];
            g_ai_qn--;
        }
    }
    if (d > 0) sl_ai_underruns++;
}

/* WHAT A READ OF AI_LEN_REG RETURNS IS **NOT DERIVABLE FROM THIS TREE**, and
 * that is stated rather than approximated. include/PR/rcp.h:616-618 documents
 * the register as R/W and gives the WRITE field layout ("[14:0] transfer
 * length ... bottom 3 bits are ignored"); it never says what a read yields.
 * Two candidates differ by exactly one buffer - about 2880 bytes here, which
 * is the whole magnitude of the defect this model exists to fix:
 *
 *   0  HEAD    the remaining byte count of the transfer IN PROGRESS only
 *   1  TOTAL   the sum still owed across both double-buffered entries
 *
 * Nothing in the tree distinguishes them. audi.c:994's underrun test
 * ("ai out of samples" at zero) constrains both equally, since both reach zero
 * together. So the choice is settled by an INDEPENDENT acceptance - the
 * cartridge command stream, byte for byte - and never by which one produces
 * nicer buffer sizes. SL_AI_LEN=1 selects TOTAL, which is also the control
 * that must break parity if the model is being exercised at all. */
unsigned int sl_audio_get_length(void)
{
    static int model = -1;
    if (model < 0) {
        const char *p = getenv("SL_AI_LEN");
        model = (p && *p == '1');
    }
    if (g_ai_qn <= 0) return 0;
    if (model) {
        unsigned int s = 0; int i;
        for (i = 0; i < g_ai_qn; i++) s += g_ai_q[i];
        return s;
    }
    return g_ai_q[0];
}

/* Bytes the device consumes per second, from what it GRANTED. Zero when no
 * device is open, which callers must treat as "no latency is measurable" and
 * never as "the latency is zero". */
static unsigned int sl_audio_device_byte_rate(void)
{
    if (!g_open) return 0;
    return g_obt_freq * g_obt_chans * g_obt_bps;
}

/* QUEUE LATENCY OVER WALL CLOCK, printed once a second under SL_AUDIO_STATS.
 *
 * A queue that accepts every byte and drains none looks identical at the
 * submit call to one that keeps up, and the difference is the whole of an
 * "effects arrive minutes late" symptom: continuous music has no external
 * timing reference, so a stream running minutes behind still sounds correct,
 * while one gunshot exposes it immediately. So the thing reported is
 * SECONDS - queued bytes divided by the device's own byte rate - alongside
 * how fast that is growing per real second.
 */
static void sl_audio_latency_tick(void)
{
    static double t0, tprev;
    static unsigned int qprev;
    static int on = -1;
    double now;
    struct timespec ts;
    unsigned int q, br;

    if (on < 0) on = (getenv("SL_AUDIO_STATS") != NULL);
    if (!on || !g_open) return;
    br = sl_audio_device_byte_rate();
    if (br == 0) return;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    now = (double) ts.tv_sec + (double) ts.tv_nsec * 1e-9;
    if (t0 == 0.0) { t0 = tprev = now; qprev = 0; return; }
    if (now - tprev < 1.0) return;
    q = (unsigned int) SDL_GetQueuedAudioSize(g_dev);
    fprintf(stderr, "AUDIOLAT wall=%6.2fs submitted=%.2fs queued=%u B = %.3fs "
                    "growth=%+.3f s/s\n",
            now - t0, (double) g_bytes / (double) br, q, (double) q / (double) br,
            ((double) q - (double) qprev) / (double) br / (now - tprev));
    tprev = now; qprev = q;
}

int sl_audio_submit(void *buf, unsigned int len)
{
    const short *s = (const short *) buf;
    unsigned int n, i;
    int block_nonzero = 0;

    if (buf == NULL || len == 0)
        return 0;

    sl_audio_dump_open();               /* in case PCM arrives before a rate is set */
    /* THE DAC CAN REFUSE. osAiSetNextBuffer returns -1 without writing either
     * register when __osAiDeviceBusy() - i.e. when AI_STATUS_FIFO_FULL, "addr
     * & len buffer full" (rcp.h:625, src/libultra/io/ai.c,
     * src/libultrare/io/aisetnextbuf.c:30). The previous model always accepted.
     * SL_AI_NOREFUSE=1 restores that, as the control. */
    sl_ai_submits++;
    /* EXACT SUBMIT STREAM. Every field here is observed at the call, never
     * inferred: the ordinal, the requested byte count as the game passed it,
     * the queue depth before and after, and whether it was accepted. Gated on
     * SL_SUBMIT_REPORT so production prints nothing. */
    {
        static int rep = -1;
        if (rep < 0) { const char *p = getenv("SL_SUBMIT_REPORT"); rep = (p && *p != '0'); }
        if (rep) fprintf(stderr, "SUBMIT n=%lu len=%u depth_before=%d\n",
                         sl_ai_submits, len, g_ai_qn);
    }
    {
        static int norefuse = -1;
        if (norefuse < 0) {
            const char *p = getenv("SL_AI_NOREFUSE");
            norefuse = (p && *p == '1');
        }
        if (!norefuse && g_ai_qn >= SL_AI_QUEUE) {
            sl_ai_refused++;
            return -1;              /* the game ignores this; the bytes are lost */
        }
        if (g_ai_qn < SL_AI_QUEUE) g_ai_q[g_ai_qn++] = len;
        else g_ai_q[SL_AI_QUEUE - 1] += len;   /* only reachable under the control */
    }
    n = len / 2;                        /* s16 samples, both channels */

    /* Statistics BEFORE any device check: we want to know what the game
     * produced even on a run with no device, because "did anything non-zero
     * ever reach the seam" is the question that matters. */
    for (i = 0; i < n; i++) {
        int v = s[i];
        if (g_swap)
            v = (short) (((v & 0xff) << 8) | ((v >> 8) & 0xff));
        if (v != 0) {
            g_nonzero++;
            block_nonzero = 1;
        }
        if (v < 0) v = -v;
        if (v > g_peak) g_peak = v;
        g_sumsq += (unsigned long long) v * (unsigned long long) v;
    }
    g_submits++;
    g_bytes  += len;
    g_frames += n / 2;
    g_blocks++;
    if (!block_nonzero)
        g_silent_blocks++;

    if (g_dump != NULL)
        fwrite(buf, 1, len, g_dump);

    if (!g_open)
        return 0;

    if (g_swap) {
        /* N64 DRAM order is big-endian; the host device wants native. */
        static short tmp[8192];
        unsigned int chunk;
        const unsigned char *p = (const unsigned char *) buf;
        unsigned int left = len;
        while (left >= 2) {
            chunk = left / 2;
            if (chunk > 8192) chunk = 8192;
            for (i = 0; i < chunk; i++)
                tmp[i] = (short) ((p[2 * i] << 8) | p[2 * i + 1]);
            SDL_QueueAudio(g_dev, tmp, chunk * 2);
            p    += chunk * 2;
            left -= chunk * 2;
        }
        sl_audio_latency_tick();
        return 0;
    }
    SDL_QueueAudio(g_dev, buf, len);
    sl_audio_latency_tick();
    return 0;
}

int sl_audio_active(void) { return g_open; }

void sl_audio_report(void)
{
    double rms;
    unsigned long long samples = g_bytes / 2;

    fprintf(stderr, "AI QUEUE submits=%lu refused=%lu underruns=%lu depth=%d\n",
            sl_ai_submits, sl_ai_refused, sl_ai_underruns, g_ai_qn);
    if (g_submits == 0) {
        fprintf(stderr, "sightline audio: no buffers were ever submitted\n");
        return;
    }
    rms = samples ? (double) g_sumsq / (double) samples : 0.0;
    rms = rms > 0.0 ? __builtin_sqrt(rms) : 0.0;

    fprintf(stderr,
            "sightline audio: submits=%llu bytes=%llu frames=%llu "
            "peak=%d rms=%.2f nonzero=%llu/%llu (%.3f%%) silent-blocks=%llu/%llu\n",
            g_submits, g_bytes, g_frames, g_peak, rms,
            g_nonzero, samples,
            samples ? 100.0 * (double) g_nonzero / (double) samples : 0.0,
            g_silent_blocks, g_blocks);
    if (g_nonzero == 0)
        fprintf(stderr, "sightline audio: OUTPUT IS DIGITAL SILENCE "
                        "(every submitted sample was zero)\n");
    /* THE LAST SEAM. Everything above is measured at the submit call, which
     * proves nothing about the device: a queue that accepted every byte and
     * drained none looks identical there. Report what SDL still holds, so
     * "bytes reached the DAC" is a measurement rather than an inference.
     * On a free-running headless fixture the queue is EXPECTED to be deep -
     * the sim produces ~15 s of audio in a fraction of a second - so a large
     * residue here is not a fault, and only a residue equal to everything
     * submitted means the device consumed nothing. */
    if (g_open)
        fprintf(stderr, "sightline audio: device queue holds %u byte(s) of %llu "
                        "submitted (%s)\n",
                (unsigned) SDL_GetQueuedAudioSize(g_dev), g_bytes,
                SDL_GetQueuedAudioSize(g_dev) < g_bytes
                    ? "the device consumed some" : "THE DEVICE CONSUMED NOTHING");
    else
        fprintf(stderr, "sightline audio: no device open - nothing was consumed "
                        "(this is trace replay's normal state)\n");
}

void sl_audio_shutdown(void)
{
    sl_audio_report();
    if (g_dump != NULL) {
        fclose(g_dump);
        g_dump = NULL;
    }
    if (g_open) {
        SDL_CloseAudioDevice(g_dev);
        g_open = 0;
    }
}
#endif
