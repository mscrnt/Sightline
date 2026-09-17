#ifndef _SL_AUDIOEV_H_
#define _SL_AUDIOEV_H_

/*
 * SIGHTLINE_AUDIO_EVENTS - an EVENT log for the audio path on the reference.
 *
 * WHY THIS AND NOT A POLL. A per-video-frame RDRAM poll sees snapshots. It
 * cannot see a transient - an allocate and free completed inside one emulator
 * step is invisible to it - so a zero from a poll can never establish an
 * absence. That is the whole reason this exists: the question being asked is
 * "does the reference ever enter the effects path", and only an event log can
 * answer it.
 *
 * WHAT IT CANNOT DO, stated here so no reader has to rediscover it. The game
 * seeds its RNG from the CPU cycle counter at boot (boss.c:389) and paces its
 * main loop against osGetCount(), so ANY image change moves the seed and ANY
 * added work changes how many game ticks fit in a frame. Measured already and
 * recorded in docs/backlog.md: the debug ROM diverges from the parity trace at
 * tick 79 with its hook not yet run even once, and ROM_SIZECTL - inert padding,
 * no code - diverges identically. An oracle inside this ROM cannot be
 * non-perturbing.
 *
 * So this log is valid for THE RUN IT PRODUCES, and its results must be scoped
 * to that run and never to the parity run. What licenses using it anyway is a
 * separate measurement: the complete ordered note-on population is byte
 * identical across seeds whose per-tick game-state traces differ, so the
 * note-on oracle survives exactly the perturbation that makes tick-paced parity
 * unachievable.
 *
 * STORAGE. A fixed region, not ordinary linked storage, because a new static
 * would shift game and audio layout and perturb more than the instrument does.
 * 0x80400000 is above every known linked, allocator-owned and source-identified
 * user: the g_mempPools table tops out at 0x002f6000, the stacks and the
 * RZIPBUFADDR decompression buffer sit below 0x003b5000, and the framebuffers
 * end exactly at 0x00400000. It deliberately stops short of 0x00500000 and
 * 0x00600000, which appear in the unreferenced LEFTOVERDEBUG tables at
 * debugmenu_handler.c:277-289 - unreferenced today, but not somewhere to squat.
 * This is an EMULATOR-ONLY region: the core provides 8 MB, and a 4 MB console
 * has no such space. The matching ROM is untouched by all of it.
 *
 * NO WRAPAROUND. The ring stops and raises `overflow` rather than discarding
 * the tail, because a silently truncated log would turn a missing event into an
 * apparent absence - the exact error this instrument exists to avoid.
 *
 * NO FORMATTING IN THE ROM. Fixed 32-byte records with source-defined type IDs;
 * the host decodes. Addresses are never logged raw - cursors go in as offsets,
 * because instrumented code and data addresses move between arms and must never
 * be compared across them.
 */

/* STORAGE: a linked array, after a fixed high address FAILED.
 *
 * The first design put the ring at a fixed 0x80400000 and the host read back
 * all zeros; moving to the uncached alias 0xA0400000 changed nothing. The
 * assumption underneath both was that physical 0x00400000 is mapped, and that
 * rested on the core REPORTING retro_get_memory_size == 0x800000. A reported
 * size is not evidence that the emulated machine has that memory mapped:
 * GoldenEye is a 4 MB title, its own map tiles exactly to 0x00400000, and
 * writes above that appear to go nowhere. I could not confirm the mapping, so
 * the fixed-address design is abandoned rather than debugged further.
 *
 * A linked array is guaranteed to be mapped, because the linker placed it. The
 * objection to ordinary storage was that it shifts game and audio layout - but
 * this image is ALREADY layout-shifted and perturbing by construction (the
 * seed moves, the tick pacing moves), its results are scoped to its own run,
 * and it is never the parity image. Within that scope the linker's guarantee is
 * worth more than the fixed address's tidiness.
 */
#define SL_AUDIOEV_MAGIC    0x534C4145u        /* 'SLAE' */
#define SL_AUDIOEV_VERSION  1u
#define SL_AUDIOEV_CAP      1024u             /* records; 32B each = 32KB */

/* Source-defined event IDs. Stable identifiers, never code addresses. */
#define SL_AEV_FRAMECOUNT   1u   /* the positive control: amClearDmaBuffers */
#define SL_AEV_NOTEON       2u   /* parsed note-on, full tuple             */
#define SL_AEV_SFX_ENTRY    3u   /* sndPlaySfx entered                     */
#define SL_AEV_SFX_INC      4u   /* g_sndAllocatedVoicesCount incremented   */
#define SL_AEV_SFX_DEC      5u   /* g_sndAllocatedVoicesCount decremented   */
#define SL_AEV_MUSIC_REQ    6u   /* musicTrack1Play entered                 */

#ifdef SIGHTLINE_AUDIO_EVENTS

typedef struct SlAudioEvHdr {
    u32 magic;
    u32 version;
    u32 capacity;
    u32 count;       /* records written */
    u32 overflow;    /* nonzero => the log is INCOMPLETE and fails loudly */
    u32 pad[3];
} SlAudioEvHdr;

typedef struct SlAudioEvRec {
    u32 type;
    u32 a, b, c, d, e, f, g;
} SlAudioEvRec;

/* STORAGE, third design - and the reason the second one was abandoned turned
 * out to be a confounded reading. MEASURED 2026-08-31, docs/backlog.md:
 *
 *   MEMPOOL_PERMANENT in the working plain image has 864 BYTES FREE. It is
 *   bounded below by _bssSegmentEnd and above by a constant fixed by the stack
 *   base, so every byte the LINK grows comes straight out of those 864. A
 *   linked 32 KB log asks for 32,800. The positive control alone - 435
 *   frame-count events at 32 bytes - needs 13,920. No record size and no
 *   capacity closes a 16x gap, so linked storage is impossible, not merely
 *   tight.
 *
 * The first design's fixed address was withdrawn because the host read back
 * all zeros. That reading is now known to be worthless: the image it was taken
 * from crashes during boot, at the first jump into the TLB-mapped .game
 * segment, and never reaches the code that writes the log. It says nothing
 * about whether the address is mapped. So the fixed address returns, and this
 * time the run that tests it is required to prove it booted first -
 * __osFaultedThread == 0 and g_AudioFrameCount == 435 - before any zero from
 * the log means anything.
 *
 * The uncached alias is deliberate: the host reads RDRAM directly, so a log
 * sitting in the data cache at the end of a run would read back stale.
 */
#define SL_AUDIOEV_ADDR     0xA0400000u

struct SlAudioEvLog { SlAudioEvHdr hdr; SlAudioEvRec rec[SL_AUDIOEV_CAP]; };
#define slAudioEvLog (*(struct SlAudioEvLog *) SL_AUDIOEV_ADDR)

/* One writer, ONE DEFINITION. It was `static` in this header, which gave each
 * of the four instrumented translation units its own copy: audi.o +240,
 * cseq.o +352, music.o +256, snd.o +336, summing to exactly the 0x4a0 by which
 * .code grew. .code is the resident segment, so that growth moves
 * _bssSegmentEnd and spends the 864-byte budget above three times over before
 * the log asks for anything. Defined once in audi.c instead. */
extern void slAudioEvPut(u32 type, u32 a, u32 b, u32 c,
                         u32 d, u32 e, u32 f, u32 g);

#define SL_AEV(t,a,b,c,d,e,f,g) slAudioEvPut((u32)(t),(u32)(a),(u32)(b),(u32)(c),\
                                             (u32)(d),(u32)(e),(u32)(f),(u32)(g))
#else
#define SL_AEV(t,a,b,c,d,e,f,g) do { } while (0)
#endif /* SIGHTLINE_AUDIO_EVENTS */

#endif /* _SL_AUDIOEV_H_ */
