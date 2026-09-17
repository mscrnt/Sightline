/**
 * Native audio output - the AI (Audio Interface) seam.
 *
 * src/game/ must never include this, exactly as for sl_gfx.h: the simulation
 * has to keep building headless for the server and the AI track. Only
 * src/platform/ and the shim call in here.
 *
 * The N64 hands FINISHED stereo PCM to the AI DAC. Three libultra entry points
 * are the entire boundary, and all three are shimmed onto one SDL2 audio
 * device:
 *
 *   osAiSetFrequency(rate)      -> open the device, report the rate obtained
 *   osAiGetLength()             -> bytes still undrained (SDL queue depth)
 *   osAiSetNextBuffer(ptr, len) -> hand the DAC the next block
 *
 * Runtime-selected like the window backend, never compile-time: one binary
 * both replays traces silently and makes noise. A device is opened ONLY when
 * a real window is up (sl_gfx_active()) or SL_AUDIO=1 forces it, so headless
 * trace replay opens no device and produces no callbacks.
 */
#ifndef SL_AUDIO_H
#define SL_AUDIO_H

#ifdef __sgi
#error "sl_audio.h is native-only; guard the include with #ifndef __sgi"
#endif

/* Opened lazily by sl_audio_set_frequency. Returns the rate actually obtained,
 * or the requested rate when no device is open (the game divides by it). */
unsigned int sl_audio_set_frequency(unsigned int rate);

/* Bytes the DAC has not drained yet. The game sizes EVERY audio frame from
 * this, so it is modelled off the fixed-step clock rather than read from the
 * host device: deterministic by construction, and identical whether or not a
 * device opened. See the long note in sl_audio.c. */
unsigned int sl_audio_get_length(void);

/* The audio arena: host memory mapped at the cartridge's own physical
 * g_musicHeap address, so the buffer addresses the manager emits into the ACMD
 * stream are the addresses the cartridge emitted. Returns NULL if it could not
 * be mapped; sl_audio_arena_live() says whether it is in force, and no address
 * parity result is valid without it. */
void *sl_audio_arena(unsigned int len);
int   sl_audio_arena_live(void);
int   sl_audio_arena_at_cartridge_base(void);

/* AI QUEUE COUNTERS, interface rather than scaffolding, for the same reason the
 * sl_acmd ungrounded counters are: a caller must be able to see that production
 * relied on a path no measurement grounds.
 *
 *   sl_ai_refused    submits the DAC turned away because both double-buffered
 *                    entries were occupied (rcp.h:607, src/libultra/io/ai.c,
 *                    src/libultrare/io/aisetnextbuf.c:30). The game ignores the
 *                    -1 (audi.c:894), so a refusal silently DROPS that buffer.
 *                    MEASURED ZERO over the 900-frame facility fixture, and the
 *                    SL_AI_NOREFUSE control is bit-identical to the baseline
 *                    there - so the two-deep limit is implemented from the
 *                    sources but is NOT exercised and NOT validated. A nonzero
 *                    value means the fixture finally reached it.
 *   sl_ai_underruns  retraces where the DAC ran dry - the condition audi.c:1002
 *                    calls "ai out of samples".
 */
extern unsigned long sl_ai_refused, sl_ai_underruns, sl_ai_submits;

/* Drain one retrace worth of samples from that model. Called once per retrace
 * from the pump, next to the game's own retrace handler, so one retrace is one
 * drain - the same discipline the input advance is held to. */
void sl_audio_ai_retrace(void);

/* Hand over len bytes of stereo s16 PCM at buf. No-op with no device. */
int sl_audio_submit(void *buf, unsigned int len);

/* 1 when a device is actually open and consuming. */
int sl_audio_active(void);

/* Measurement: peak/RMS/non-zero counts over everything submitted so far.
 * Printed at exit, because "callbacks serviced" says nothing about whether the
 * samples were anything but zeros. */
void sl_audio_report(void);

void sl_audio_shutdown(void);

#endif /* SL_AUDIO_H */
