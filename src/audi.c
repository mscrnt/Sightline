#include <ultra64.h>
#include <PR/os.h>
#include "sched.h"
#include "audi.h"
#include "thread_config.h"
#include "bondgame.h"
#include "speed_graph.h"
#ifdef SIGHTLINE_AUDIO_EVENTS
#include "sl_audioev.h"
#endif
#ifdef SIGHTLINE_AUDIO_EVENTS
/* THE SINGLE DEFINITION of the event writer. It used to be `static` in
 * sl_audioev.h, which instantiated it in all four instrumented translation
 * units and cost 0x4a0 of .code - three times the whole layout budget. The
 * storage it writes is not linked at all any more; see sl_audioev.h.
 *
 * IDO is C89: every declaration comes before the first statement. */
void slAudioEvPut(u32 type, u32 a, u32 b, u32 c, u32 d, u32 e, u32 f, u32 g)
{
    SlAudioEvHdr *h;
    SlAudioEvRec *r;

    h = &slAudioEvLog.hdr;
    if (h->magic != SL_AUDIOEV_MAGIC) {
        h->magic = SL_AUDIOEV_MAGIC;
        h->version = SL_AUDIOEV_VERSION;
        h->capacity = SL_AUDIOEV_CAP;
        h->count = 0;
        h->overflow = 0;
    }
    /* No wraparound: a silently truncated log turns a missing event into an
     * apparent absence, which is the one error this instrument exists to
     * avoid. */
    if (h->count >= h->capacity) {
        h->overflow = h->overflow + 1;
        return;
    }
    r = &slAudioEvLog.rec[h->count];
    r->type = type; r->a = a; r->b = b; r->c = c;
    r->d = d; r->e = e; r->f = f; r->g = g;
    h->count = h->count + 1;
}
#endif

#ifdef SL_AUDIOEV_CODECTL
/* THE CONTROL FOR THE AUDIO EVENT LOG'S CODE FOOTPRINT, and the one control
 * the previous pass named but did not run.
 *
 * SL_ROMDBG_SIZECTL is not this control. Its padding is a `const` array in
 * lv.c, so it lands in rodata of the TLB-paged .game segment: it grows
 * _bssSegmentEnd and leaves .code alone. The audio event log's cost is the
 * opposite shape - audi.c, music.c, snd.c and libultra/audio/cseq.c are all in
 * .code, the RESIDENT segment, so instrumenting them grows .code and moves
 * _codeSegmentEnd, __dataSegmentVaddrStart, and the .cdata/.csegment load
 * addresses with it. That difference is the only structural one left standing
 * between the debug ROM that boots and the audio ROM that does not.
 *
 * So this is inert growth OF .code: a function that is never called from
 * anywhere, in one of the same translation units, sized to about what the
 * instrumentation costs. No reachable code, no new state, no behaviour.
 *
 * Reading it. If an image with this flag ALSO fails to boot, the mechanism is
 * .code growth itself and the audio path cannot be instrumented in ROM at all,
 * because there is no way to log an event there without emitting an
 * instruction in .code. If it boots, .code growth is exonerated and the audio
 * ROM's fault is something else - which is still a finding, and the logger is
 * retired either way.
 *
 * Volatile stores to a fixed address: nothing here may be folded, and it adds
 * no linked data of its own, so the growth is .text and only .text.
 *
 * SIZED DELIBERATELY SMALL. A first pass at 70 stores grew .code by 0x350 -
 * 848 bytes, which is 16 under the 864-byte pool budget - and failed to boot.
 * That result was confounded: it could not distinguish "the .code segment
 * grew" from "the permanent pool ran out". Six stores put the growth far
 * below the budget, so a failure at this size cannot be an exhaustion. */
void slAudioEvCodeCtl(void)
{
    volatile u32 *p = (volatile u32 *) 0xA0400000u;

    p[0] = 0x5A000000u;
    p[1] = 0x5A010007u;
    p[2] = 0x5A02000Eu;
    p[3] = 0x5A030015u;
    p[4] = 0x5A04001Cu;
    p[5] = 0x5A050023u;
}
#endif

/**
 * EU .data, offset from start of data_seg : 0x23A0
*/

/**
 * @file audi.c
 * This file contains audio code. Starts main audio thread, handles some audio DMA.
 */

// 0x5622 = 22050
#define OUTPUT_RATE                    0x5622

#ifdef REFRESH_PAL
/* PAL */
#define MAYBE_FRAME_RATE                   50
#else
/* NTSC */
#define MAYBE_FRAME_RATE                   60
#endif


#define FRAMES_PER_FIELD_AS_POW2            1
#define AUDIO_FRAME_MESSAGE_QUEUE_SIZE      8
#define AUDIO_REPLY_MESSAGE_QUEUE_SIZE      8
#define AUDIO_DMA_IO_QUEUE_SIZE            64
#define AUDIO_DMA_QUEUE_SIZE               66
#define AUDIO_DMA_MAX_BUFFER_LENGTH     0x200

#define NUMBER_OUTPUT_BUFFERS               3
#define NUMBER_ACMD_LISTS                   2
#define MAX_ACMD_SIZE                    3000
#define NUMBER_DMA_BUFFERS                 64
#define EXTRA_SAMPLES                    0x25
#define AUDIO_FRAME_MESSAGE_QUEUE_SIZE      8
#define AUDIO_REPLY_MESSAGE_QUEUE_SIZE      8

#define MAIN_QUIT_MESSAGE                  10
#define AUDIO_MANAGER_COUNT_INTERVAL     0xf0

extern long long int rspbootTextStart[];
extern long long int gsp3DTextStart[];
extern long long int aspMainTextStart[];
extern long long int aspMainDataStart[];
extern u8 sp_audi[];

/**
 * Copied from the n64devkit audio examples.
 * sizeof(struct DMABuffer_s) == 0x14 (20)
 */
typedef struct DMABuffer_s {
    /**
     * 0x0.
     */
    ALLink node;

    /**
     * 0x8.
     */
    int startAddr;

    /**
     * 0xc.
     */
    u32 lastFrame;

    /**
     * 0x10.
     */
    u8* ptr;
} DMABuffer;

/**
 * Copied from the n64devkit audio examples.
 * sizeof(struct DMAState_s) == 0xc (12).
 */
typedef struct DMAState_s {
    /**
     * This was defined (in the devkit) as u8 (and code expects a byte), but the size
     * of the struct and offset for firstUsed seems to make this u32/s32.
     * I'm adding the union to make this explicit.
     * 0x0.
     */
    union {
        u8 initialized;
        s32 _unusedAlign;
    } u;

    /**
     * 0x4.
     */
    DMABuffer *firstUsed;

    /**
     * 0x8.
     */
    DMABuffer *firstFree;
} DMAState;

/**
 * Copied from the n64devkit audio examples.
 */
typedef union AudioMessage_u {
    struct {
        s16 type;
    } gen;

    struct {
        s16 type;
        struct AudioInfo_s *info;
    } done;

    OSScMsg app;
} AudioMessage;

/**
* Modified from n64devkit example.
* sizeof(struct _DMAState) == 0xc (12).
*/
typedef struct AudioInfo_s {
    /**
    * Output data pointer.
    * 0x0.
    */
    s16 *data;

    /**
     * # of samples synthesized in this frame
     * 0x4.
     */
    s16 frameSamples;

    /**
     * scheduler structure
     * 0x8
     */
    OSScTask task;
} AudioInfo;

// unknown purpose
u32 D_800230F0 = 0;

u32 g_AudioFrameCount = 0;

u32 g_NextDMa = 0;

u32 g_CurrentAcmdList = 0;

/*
* This macro is used/defined in both libultra and libnaudio
*/
#define ms *(((s32)((f32)44.1)) & ~0x7)

#define CUSTOM_FX_SECTION_COUNT   6
#define CUSTOM_FX_SECTION_SIZE    8
/*
* Following the libultra and libnaudio naming convention ...
*/
s32 CUSTOM_FX_PARAMS_N[CUSTOM_FX_SECTION_COUNT * CUSTOM_FX_SECTION_SIZE + 2] = {

    /* sections	   length */
             6,     160 ms,

    /*                                         chorus  chorus   filter
    input    output  fbcoef  ffcoef    gain     rate   depth    coef  */
        0,     4 ms,   9830,  -9830,      0,        0,     0,       0,
     4 ms,     8 ms,   9830,  -9830, 0x2B84,        0,     0,  0x2500,
    20 ms,    64 ms,  16384, -16384, 0x11EB,        0,     0,  0x3000,
    80 ms,   140 ms,  16384, -16384, 0x11EB,        0,     0,  0x3500,
    84 ms,   120 ms,   8192,  -8192,      0,        0,     0,  0x4000,
        0,   148 ms,  13000, -13000,      0,   0x017C,   0xA,  0x4500
};

s32 g_FirstTime = 1;

/*bss needs fixing */
s32 dword_CODE_bss_8005E4B0[2];

/**
 * Address 8005E4B8.
 * (type is u64)
 * Used in amMain.
 * This looks like it stores the largest sDeltaTime between
 * counts of AUDIO_MANAGER_COUNT_INTERVAL.
 */
OSTime g_LargestDeltaTime;

/**
 * Address 8005E4C0.
 * (type is u64)
 * Used in amMain.
 * Stores the elpased time of main loop (difference between sEndTime and sStartTime).
 */
OSTime g_DeltaTime;


/**
 * Address 8005E4C8.
 * Every AUDIO_MANAGER_COUNT_INTERVAL number of events, the average for sDeltaTimeSum
 * is computed and stored here.
 */
u64 g_DeltaAverage;


/**
 * Address 8005E4D0.
 * Tracks the sum total elapsed time. Reset every AUDIO_MANAGER_COUNT_INTERVAL.
 */
u64 g_DeltaTimeSum;

/**
 * Address 8005E4D8.
 * (type is u64)
 * Used in amMain.
 * Stores the time at the start of the loop.
 */
OSTime g_StartTime;

/**
 * Address 8005E4E0.
 * (type is u64)
 * Used in amMain.
 * Stores the time after primary processing is done.
 */
OSTime g_EndTime;

/**
 * Unknown / unused
 */
char dword_CODE_bss_8005E4E8[0x30];

/**
 * Address 8005e518.
 * sizeof(struct AudioManager_s) == 0x288 (648)
 */
struct AudioManager_s {

    /**
     * 0.
     */
    Acmd *cmdList[NUMBER_ACMD_LISTS];

    /**
     * 0x8.
     */
    AudioInfo *audioInfo[NUMBER_OUTPUT_BUFFERS];

    /**
    * 0x14.
    */
    u32 numberOutputBuffers;

    /**
     * 0x18.
     */
    OSThread audioThread;

    /**
     * 0x1c8.
     */
    OSMesgQueue frameMessageQueue;

    /**
     * 0x1e0.
     */
    OSMesg frameMessageBuffer[AUDIO_FRAME_MESSAGE_QUEUE_SIZE];

    /**
     * 0x200.
     */
    OSMesgQueue replyMessageQueue;

    /**
     * 0x218.
     */
    OSMesg replyMessageBuffer[AUDIO_REPLY_MESSAGE_QUEUE_SIZE];

    /**
     * 0x238
     */
    ALGlobals g;

} g_AudioManager;

/**
 * Address 0x8005e7a0.
 */
OSScClient g_AudioClient[2];

/**
 * Address 0x8005e7b0.
 */
DMAState g_DmaState;

DMABuffer g_DmaBuffers[NUMBER_DMA_BUFFERS];

u32 g_MinFrameSize;
u32 g_FrameSize;
u32 g_MaxFrameSize;
s32 g_CommandLength;

OSIoMesg g_DmaIOMessageBuffer[AUDIO_DMA_IO_QUEUE_SIZE];

OSMesgQueue g_DmaMessageQueue;

OSMesg g_DmaMessageBuffer[AUDIO_DMA_QUEUE_SIZE];


// Forward declarations
#ifndef __sgi
/* fatal-error helper in the platform layer: audi.c cannot include host stdio
 * (the SDK include tree shadows the compiler headers) */
extern void sl_fatalf(const char *fmt, unsigned v);
#endif
s32 amDmaCallback(s32 addr, s32 len, void* state);
void amClearDmaBuffers(void);
void amHandleFrameMessage(AudioInfo *info, AudioInfo *lastInfo);
void amHandleDoneMessage(AudioInfo *info);
void amMain(void* arg);
ALDMAproc amDmaNew(DMAState** state);


/**
 * Address 29D0 70001BD0
 *
 * Looks to be loosely based on method
 *     amCreateAudioMgr
 * from the n64devkit.
 *
 * @param alconf hw setup/config.
 */
void amCreateAudioManager(ALSynConfig* alconf)
{
    u32 j;
    f32 fsize;

    alconf->dmaproc = &amDmaNew;
    alconf->outputRate = osAiSetFrequency(OUTPUT_RATE);

    fsize = (f32) ((alconf->outputRate << FRAMES_PER_FIELD_AS_POW2) / (f32)MAYBE_FRAME_RATE);

    g_FrameSize = (u32) fsize;

    if (g_FrameSize < fsize)
    {
        g_FrameSize++;
    }

    // This rounds up to the next multiple of 16.
    if (g_FrameSize & 0xf)
    {
        g_FrameSize = (g_FrameSize & ~0xf) + 0x10;
    }

    g_MinFrameSize = (u32)(g_FrameSize - 0x10);
    g_MaxFrameSize = (u32)(g_FrameSize + EXTRA_SAMPLES + 0x10);

    if (alconf->fxType == AL_FX_CUSTOM)
    {
#ifdef __sgi
        s32 sp48[CUSTOM_FX_SECTION_COUNT * CUSTOM_FX_SECTION_SIZE + 2] = CUSTOM_FX_PARAMS_N;
#else
        /* Sightline native: IDO accepts initialising an array from another
         * ARRAY VARIABLE and emits a plain word copy - build/u/src/audi.o
         * disassembles to an unrolled lw/sw loop over 192 bytes plus a
         * two-word tail, i.e. exactly sizeof(sp48). GCC rejects the syntax
         * ("invalid initializer"), so spell the same copy out. This was the
         * single "one initializer fix pending" that kept audi.c out of the
         * native build. Guarded so the matching build is byte-identical. */
        s32 sp48[CUSTOM_FX_SECTION_COUNT * CUSTOM_FX_SECTION_SIZE + 2];
        bcopy(CUSTOM_FX_PARAMS_N, sp48, sizeof(sp48));
#endif
        alconf->params = sp48;
        alInit(&g_AudioManager.g, alconf);
    }
    else
    {
        alInit(&g_AudioManager.g, alconf);
    }

    for (j=0; j < NUMBER_OUTPUT_BUFFERS; j++)
    {
        g_AudioManager.audioInfo[j]       = (AudioInfo *)alHeapAlloc(alconf->heap, 1, sizeof(AudioInfo));
        g_AudioManager.audioInfo[j]->data = (s16 *)alHeapAlloc(alconf->heap, 1, g_MaxFrameSize * 4);
    }

    osCreateMesgQueue(&g_AudioManager.replyMessageQueue, (OSMesg *)&g_AudioManager.replyMessageBuffer, AUDIO_REPLY_MESSAGE_QUEUE_SIZE);
    osCreateMesgQueue(&g_AudioManager.frameMessageQueue, (OSMesg *)&g_AudioManager.frameMessageBuffer, AUDIO_FRAME_MESSAGE_QUEUE_SIZE);
    osCreateMesgQueue(&g_DmaMessageQueue, (OSMesg *)&g_DmaMessageBuffer, AUDIO_DMA_IO_QUEUE_SIZE);

    g_DmaBuffers[0].node.prev = NULL;
    g_DmaBuffers[0].node.next = NULL;

    for (j = 0; (s32)j < NUMBER_DMA_BUFFERS - 1; j++)
    {
        alLink((ALLink *)&g_DmaBuffers[j + 1], (ALLink *)&g_DmaBuffers[j]);
        g_DmaBuffers[j].ptr = (void *)alHeapAlloc(alconf->heap, 1, AUDIO_DMA_MAX_BUFFER_LENGTH);
    }
    // last buffer already linked, but still needs buffer
    g_DmaBuffers[j].ptr = (void *)alHeapAlloc(alconf->heap, 1, AUDIO_DMA_MAX_BUFFER_LENGTH);

    for (j = 0; j < NUMBER_ACMD_LISTS; j++)
    {
        g_AudioManager.cmdList[j] = (Acmd *)alHeapAlloc(alconf->heap, 1, MAX_ACMD_SIZE * sizeof(Acmd));
    }

    osCreateThread(&g_AudioManager.audioThread, AUDI_THREAD_ID, &amMain, 0, (void*)setSPToEnd((u8*)(&sp_audi), sizeof(sp_audi)), AUDI_THREAD_PRIORITY);
}

/**
 * 2B58 70001F58
 * insert sound manager thread
 *	redirect to 7000D580: A0=8005E530
 */
void amStartAudioThread(void)
{
    osStartThread(&g_AudioManager.audioThread);
#ifndef __sgi
    /* osStartThread is a no-op shim natively (sl_ultra_shim.c:354), so amMain
     * never runs and its PROLOGUE never executes - which is why the audio
     * client was never on the scheduler's list and the frame queue stayed
     * empty. Starting the thread means running that prologue; the loop body is
     * driven a step at a time from the pump. See sl_audio_cooperative_start. */
    { extern void sl_audio_cooperative_start(void);
      sl_audio_cooperative_start(); }
#endif
}

/**
 * 2B7C 70001F7C
 * Looks to be loosely based on method
 *     __amMain
 * from the n64devkit. This method makes some kind of video calls,
 * but also does some kind of debug tracking of the time spent between
 * beginning and end of processing.
 *
 * @param arg unused.
 */
#ifndef __sgi
/* COOPERATIVE AUDIO MANAGER - amMain's PROLOGUE and its LOOP BODY, run from the
 * frame pump instead of a thread. amMain (below) is a blocking message loop and
 * cannot be called as a one-shot: it would deadlock on its second receive. This
 * reuses the game's OWN handlers, queues and state rather than reimplementing
 * them.
 *
 * Ordering is amMain's, derived and cited in docs/backlog.md: the slot is
 * audioInfo[g_AudioFrameCount % 3] taken with the PRE-advance value
 * (audi.c:530); amHandleFrameMessage advances the counter via amClearDmaBuffers
 * (595 -> 928); the emitted task carries its own AudioInfo as task.msg
 * (644-645), which is what a completion returns.
 *
 * The retrace CONDITION is deliberately not re-implemented here.
 * __scHandleRetrace runs the game's own client-notify loop, so this step acts
 * only when that loop actually queued a message for the audio client - which is
 * what makes the every-second-retrace cadence come from live state. */
static AudioInfo *sl_am_lastInfo = 0;
static int sl_am_started = 0;
static int sl_am_stopped = 0;

#ifdef SL_ALHEAP_TRACE
/* MEASUREMENT AND CONTROLS - compiled only under SL_ALHEAP_TRACE, so production
 * carries none of it. audi.c is a decomp file and the repo shadows <stdio.h>
 * with a minimal N64 one, so nothing here does I/O: these are plain counters,
 * read and printed from the platform layer (sl_main.c). */
unsigned long sl_am_steps, sl_am_frames, sl_am_completions, sl_am_slotadv;
unsigned long sl_am_qual_n, sl_am_qual_enq, sl_am_nonqual_n, sl_am_nonqual_enq;
unsigned long sl_am_notify_bad, sl_am_lastinfo_adv, sl_am_missing_completion;
unsigned long sl_am_reg_hits, sl_am_reg_clients, sl_am_reg_nextword;
unsigned long sl_am_ctl_fired;
/* ORDERED SEQUENCES, not counters. A control that only moves a total cannot
 * show WHERE the lifecycle diverged, and "a counter changed" is exactly the
 * evidence the brief rules out. Each is the first 32 entries.
 *   slot_seq     the triple-buffer slot each frame handling chose
 *   evt_seq      the scheduler frameCount the step ran on - the event identity
 *   lastinfo_seq the slot of the lastInfo passed to amHandleFrameMessage,
 *                -1 for NULL; this is what osAiSetNextBuffer consumes */
unsigned long sl_am_slot_seq[32], sl_am_slot_seq_n;
unsigned long sl_am_evt_seq[32];
long sl_am_lastinfo_seq[32];
u32 sl_am_first_slot_bad;
static s32 sl_am_qpre;
extern int sl_audio_ctl_mode(void);      /* SL_AUDIO_CTL, read in the shim */

/* Called by the pump either side of __scHandleRetrace. The notify loop
 * (sched.c:333-337) runs INSIDE that handler and reads the POST-increment
 * frameCount (sched.c:319), so the classification uses the value the loop saw. */
void sl_audio_observe_pre(void)
{
    sl_am_qpre = g_AudioManager.frameMessageQueue.validCount;
}

void sl_audio_observe_post(void)
{
    s32 d = g_AudioManager.frameMessageQueue.validCount - sl_am_qpre;

    if (!sl_am_started) return;             /* nothing registered: not a sample */
    if ((os_scheduler.frameCount & 1) == 0) {
        sl_am_qual_n++;
        if (d == 1) sl_am_qual_enq++; else sl_am_notify_bad++;
    } else {
        sl_am_nonqual_n++;
        if (d == 0) sl_am_nonqual_enq++; else sl_am_notify_bad++;
    }
}

/* Registration proof taken from the SCHEDULER'S OWN client list, not from a
 * flag this file set: walk sc->clientList and count how many times the audio
 * client appears, plus the word the notify loop actually reads. */
void sl_audio_registration_probe(void)
{
    OSScClient *c;

    sl_am_reg_hits = 0;
    sl_am_reg_clients = 0;
    for (c = os_scheduler.clientList; c != 0; c = c->next) {
        sl_am_reg_clients++;
        if (c == &g_AudioClient[0]) sl_am_reg_hits++;
        if (sl_am_reg_clients > 64) break;  /* a cycle must not hang the probe */
    }
    sl_am_reg_nextword = (unsigned long) *((s32 *) &g_AudioClient[0] + 2);
}
#endif /* SL_ALHEAP_TRACE */

/* THE PROLOGUE. amMain does exactly two things before `while (!done)`:
 *
 *   locals   count=0, done=0, msg=NULL, info=NULL            audi.c:597-600
 *   osScAddClient(&os_scheduler, &g_AudioClient[0],
 *                 &g_AudioManager.frameMessageQueue, 1)      audi.c:602
 *
 * `count` and `done` are loop-private - count feeds only the speedgraph
 * delta-time averages inside the loop body, done is the loop condition - so a
 * form with no loop has no consumer for either. `msg` is overwritten by the
 * receive before it is read. `info` IS live across iterations: it is
 * amHandleFrameMessage's `lastInfo` argument and must start NULL, which
 * sl_am_lastInfo does.
 *
 * The registration is the only prologue operation whose state outlives the
 * function. It puts g_AudioClient[0] on the scheduler's client list with
 * next==1 - the word the notify loop reads (sched.c:334) to signal audio on
 * even frame counts only. Nothing else in this build performs it: tree-wide,
 * `grep -rn osScAddClient src/ include/ tools/` finds the definition
 * (sched.c:193), the declaration (sched.h:88) and exactly two calls - init.c:246
 * registering gfxClient with NULL, and audi.c:602 here.
 *
 * It must run exactly once and at amStartAudioThread time: the queue it
 * registers is created by amCreateAudioManager (audi.c:390) and the scheduler
 * it registers with by osCreateScheduler (init.c:239/243, reached from mainproc
 * before bossEntry), so both are live by then and neither is before. */
void sl_audio_cooperative_start(void)
{
    OSScClient *c;

    if (sl_am_started)
        sl_fatalf("sightline native: audio cooperative start called twice (%u)\n", 1);
    /* osCreateScheduler is the only writer of retraceMsg.type (sched.c:172). */
    if (os_scheduler.retraceMsg.type != OS_SC_RETRACE_MSG)
        sl_fatalf("sightline native: audio start before osCreateScheduler (%u)\n",
                  (unsigned) os_scheduler.retraceMsg.type);
    /* amCreateAudioManager is the only creator of this queue (audi.c:390). */
    if (g_AudioManager.frameMessageQueue.msgCount != AUDIO_FRAME_MESSAGE_QUEUE_SIZE)
        sl_fatalf("sightline native: audio start before amCreateAudioManager (%u)\n",
                  (unsigned) g_AudioManager.frameMessageQueue.msgCount);
    /* Duplicate registration is unsupported and must not pass quietly:
     * osScAddClient prepends unconditionally, so a second call would leave
     * c->next pointing into a list that already contains c - a cycle the
     * notify loop walks forever. The scheduler's list is the authority here,
     * not sl_am_started. */
    for (c = os_scheduler.clientList; c != 0; c = c->next)
        if (c == &g_AudioClient[0])
            sl_fatalf("sightline native: audio client already registered (%u)\n", 1);

    sl_am_lastInfo = 0;
    sl_am_stopped = 0;
    osScAddClient(&os_scheduler, &g_AudioClient[0],
                  &g_AudioManager.frameMessageQueue, (OSScClient *) 1);
    sl_am_started = 1;
}

/* ONE ITERATION of amMain's loop body. */
void sl_audio_step(void)
{
    extern int sl_sc_complete_for(void *q);
    AudioInfo *info;
    AudioInfo *done = 0;
    OSScMsg *msg = 0;
#ifdef SL_ALHEAP_TRACE
    int ctl = sl_audio_ctl_mode();
    u32 count_before;
#endif

    if (!sl_am_started || sl_am_stopped) return;
    if (g_AudioManager.frameMessageQueue.validCount <= 0) return;

#ifdef SL_ALHEAP_TRACE
    /* CONTROL 2 - suppress ONE qualifying notification: consume the message the
     * notify loop queued and do nothing with it. The lifecycle must lose one
     * whole task, not merely one counter. */
    if (ctl == 2 && sl_am_ctl_fired == 0 && sl_am_steps == 4) {
        sl_am_ctl_fired = 1;
        osRecvMesg(&g_AudioManager.frameMessageQueue, (OSMesg *) &msg, OS_MESG_NOBLOCK);
        return;
    }
#endif
    osRecvMesg(&g_AudioManager.frameMessageQueue, (OSMesg *) &msg, OS_MESG_NOBLOCK);

    /* amMain's switch (audi.c:606). Unsupported message types must fail loudly
     * rather than be dropped - a scheduler message this driver does not model
     * is exactly the kind of silent skip the harness cannot see. */
    switch (msg->type) {
    case OS_SC_RETRACE_MSG:
        break;
    case OS_SC_PRE_NMI_MSG:
    case MAIN_QUIT_MESSAGE:
        sl_am_stopped = 1;              /* amMain's `done = 1`, then alClose */
        alClose(&g_AudioManager.g);
        return;
    default:
        sl_fatalf("sightline native: audio manager: unmodelled scheduler "
                  "message type %u\n", (unsigned) msg->type);
        return;
    }

#ifdef SL_ALHEAP_TRACE
    sl_am_steps++;
    count_before = g_AudioFrameCount;
    /* CONTROL 4 - NEGATIVE control on the triple-buffer writer: advance the
     * counter HERE, in the driver, which is the wrong place. If the slot
     * progression were an artefact of pump count rather than of
     * amClearDmaBuffers' single advance (audi.c:928), this would not change
     * it. It does: 0 1 2 0 1 2 becomes 0 2 1 0 2 1. */
    if (ctl == 4) { g_AudioFrameCount++; sl_am_ctl_fired++; }
    if (sl_am_slot_seq_n < 32) {
        unsigned long k = sl_am_slot_seq_n;
        sl_am_slot_seq[k] = (unsigned long) (g_AudioFrameCount % 3);
        sl_am_evt_seq[k] = (unsigned long) os_scheduler.frameCount;
        sl_am_lastinfo_seq[k] =
            sl_am_lastInfo == 0 ? -1
          : sl_am_lastInfo == g_AudioManager.audioInfo[0] ? 0
          : sl_am_lastInfo == g_AudioManager.audioInfo[1] ? 1
          : sl_am_lastInfo == g_AudioManager.audioInfo[2] ? 2 : -2;
        sl_am_slot_seq_n = k + 1;
    }
#endif

    info = g_AudioManager.audioInfo[g_AudioFrameCount % 3];
    amHandleFrameMessage(info, sl_am_lastInfo);

#ifdef SL_ALHEAP_TRACE
    /* CONTROL 3 - DUPLICATE one qualifying notification: run the loop body a
     * second time for a single notify event. */
    if (ctl == 3 && sl_am_ctl_fired == 0 && sl_am_steps == 4) {
        sl_am_ctl_fired = 1;
        amHandleFrameMessage(g_AudioManager.audioInfo[g_AudioFrameCount % 3],
                             sl_am_lastInfo);
        sl_sc_complete_for(&g_AudioManager.replyMessageQueue);
        { AudioInfo *d2 = 0;
          if (osRecvMesg(&g_AudioManager.replyMessageQueue, (OSMesg *) &d2,
                         OS_MESG_NOBLOCK) == 0 && d2) {
              amHandleDoneMessage(d2); sl_am_lastInfo = d2; }
        }
    }
#endif

#ifdef SL_ALHEAP_TRACE
    /* The slot counter must be advanced by amClearDmaBuffers INSIDE the
     * handler (audi.c:595 -> 928), never by anything in the pump. */
    if (g_AudioFrameCount == count_before + 1) sl_am_slotadv++;
    else if (sl_am_first_slot_bad == 0) sl_am_first_slot_bad = (u32) sl_am_steps;
    sl_am_frames++;
#endif

    /* amMain's BLOCKING receive on the reply queue (audi.c:633): the manager
     * does not proceed until the RSP has finished the task it just submitted.
     * Cooperatively that means running that task to completion HERE, not
     * leaving its reply for a later pump - which would put lastInfo a frame
     * behind and hand osAiSetNextBuffer the wrong buffer. The completion is
     * produced by the existing shim task path, not by a second driver. */
    sl_sc_complete_for(&g_AudioManager.replyMessageQueue);

#ifdef SL_ALHEAP_TRACE
    /* CONTROL 1 - WITHHOLD one real completion. The task IS completed (so no
     * reply is left dangling and nothing else is perturbed); the manager
     * simply never handles it. amHandleDoneMessage does not run for it and
     * lastInfo cannot advance to it. Its later consumer is the NEXT
     * amHandleFrameMessage, which hands lastInfo to osAiSetNextBuffer
     * (audi.c:600) - so lastinfo_seq must diverge from the baseline and STAY
     * diverged, not merely dip by one. */
    if (ctl == 1 && sl_am_ctl_fired == 0 && sl_am_steps == 4) {
        sl_am_ctl_fired = 1;
        osRecvMesg(&g_AudioManager.replyMessageQueue, (OSMesg *) &done,
                   OS_MESG_NOBLOCK);
        return;
    }
#endif

    if (osRecvMesg(&g_AudioManager.replyMessageQueue, (OSMesg *) &done,
                   OS_MESG_NOBLOCK) == 0 && done) {
        amHandleDoneMessage(done);
        sl_am_lastInfo = done;
#ifdef SL_ALHEAP_TRACE
        sl_am_completions++;
        sl_am_lastinfo_adv++;
#endif
    } else {
        /* amMain would block here forever. Natively a submitted audio task
         * that produced no completion is an unmodelled state, not a frame to
         * skip. */
#ifdef SL_ALHEAP_TRACE
        sl_am_missing_completion++;
        if (ctl != 0) return;
#endif
        sl_fatalf("sightline native: audio task produced no completion "
                  "(step %u)\n", (unsigned) g_AudioFrameCount);
    }
}
#endif

void amMain(void* arg)
{
	s32 count = 0;
	s32 done = 0;
	s16 *msg = NULL;
	AudioInfo *info = NULL;

	osScAddClient(&os_scheduler, &g_AudioClient[0], &g_AudioManager.frameMessageQueue, 1);

	while (!done) {
		osRecvMesg(&g_AudioManager.frameMessageQueue, (OSMesg *) &msg, OS_MESG_BLOCK);

		switch (*msg) {
		case OS_SC_RETRACE_MSG:
			g_StartTime = osGetTime();
		    speedgraphMarkerHandler(0x30000);
			amHandleFrameMessage(g_AudioManager.audioInfo[g_AudioFrameCount % 3], info);
			count++;
            speedgraphMarkerHandler(0x60000);

			g_EndTime = osGetTime();
			g_DeltaTime = g_EndTime - g_StartTime;

			if (count % AUDIO_MANAGER_COUNT_INTERVAL == 0) {
				g_DeltaAverage = g_DeltaTimeSum / AUDIO_MANAGER_COUNT_INTERVAL;

                // comma is required to continue into next statement, or will fail to match.
                // Or can have two statements on the same line.
                g_DeltaTimeSum = 0,
                g_LargestDeltaTime = 0;
			} else {
				g_DeltaTimeSum = (g_DeltaTimeSum + g_EndTime) - g_StartTime;
			}

			if (g_LargestDeltaTime < g_EndTime - g_StartTime) {
				g_LargestDeltaTime = g_EndTime - g_StartTime;
			}

			osRecvMesg(&g_AudioManager.replyMessageQueue, (OSMesg *) &info, OS_MESG_BLOCK);

			amHandleDoneMessage(info);
			break;
		case 5:
			done = 1;
			break;
		case MAIN_QUIT_MESSAGE:
			done = 1;
			break;
		}
	}

	alClose(&g_AudioManager.g);
}

/**
 * 2E44	70002244
 * Based on method
 *     static u32 __amHandleFrameMsg(AudioInfo *info, AudioInfo *lastInfo)
 * from the n64devkit demos_old/simple/audiomgr.c.
 *
 * original documentation:
 * First, clear the past audio dma's, then calculate
 * the number of samples you will need for this frame. This value varies
 * due to the fact that audio is synchronised off of the video interupt
 * which can have a small amount of jitter in it. Varying the number of
 * samples slightly will allow you to stay in synch with the video. This
 * is an advantageous thing to do, since if you are in synch with the
 * video, you will have fewer graphics yields. After you've calculated
 * the number of frames needed, call alAudioFrame, which will call all
 * of the synthesizer's players (sequence player and sound player) to
 * generate the audio task list. If you get a valid task list back, put
 * it in a task structure and send a message to the scheduler to let it
 * know that the next frame of audio is ready for processing.
 *
 * @param info audio info.
 * @param lastInfo last info.
 */
void amHandleFrameMessage(AudioInfo *info, AudioInfo *lastInfo)
{
    s16* outBuffer;
    Acmd *cmdlp;
    s32 temp_v1;

    /* call once a frame, before doing alAudioFrame */
    amClearDmaBuffers();

    outBuffer = (s16*)osVirtualToPhysical(info->data);

    if (lastInfo)
    {
        osAiSetNextBuffer(lastInfo->data, lastInfo->frameSamples * 4);
    }

    /* calculate how many samples needed for this frame to keep the DAC full */
    /* this will vary slightly frame to frame, must recalculate every frame */
    /* divide by four, to convert bytes */
    /* to stereo 16 bit samples */
    info->frameSamples = (u16)(((g_FrameSize - (osAiGetLength() >> 2)) + 16 + EXTRA_SAMPLES) & ~0xf);
    temp_v1 = g_MinFrameSize;

    if ((s32)info->frameSamples < (s32)(s16)temp_v1)
    {
        info->frameSamples = (s16)temp_v1;
    }

    cmdlp = (Acmd*)alAudioFrame(g_AudioManager.cmdList[g_CurrentAcmdList], &g_CommandLength, outBuffer, info->frameSamples);

#if defined(SL_ALHEAP_TRACE) && !defined(__sgi)
    /* Measurement scaffolding, gated by the same flag as the other audio
     * reporters. Guarding the DEFINITION while leaving this call live turned
     * sl_acmd_capture into a fresh unresolved stub - 57 where 56 was correct.
     * Both sides move together or neither does.
     * NATIVE ACMD CAPTURE, at exactly the seam acmd_census.py reads on the
     * cartridge: AFTER alAudioFrame has built the list (just above) and BEFORE
     * osSendMesg hands it to the RSP (below). The census reads
     * cmdList[g_CurrentAcmdList ^ 1] because it samples RDRAM after audi.c
     * flips the index; from inside this function, before that flip, the list
     * just built is the UN-flipped cmdList[g_CurrentAcmdList]. Same list, two
     * vantage points - getting this wrong would capture next frame's buffer.
     * Command count is (cmdlp - cmdList)/8, matching the data_size expression
     * below. Off unless SL_ACMD_OUT names a file. */
    /* frameSamples is what alAudioFrame is SIZED BY, so it is upstream of
     * every buffer length in the emitted list. The cartridge's varies 720..784
     * under AI-FIFO feedback (measured); printing the native sequence is what
     * makes the two comparable instead of inferred from opcode counts. */
    /* NO I/O HERE. audi.c is a decomp file and the repo shadows <stdio.h> with
     * a minimal N64 one, so a bare fprintf does not compile - the same reason
     * the other reporters in this file are plain counters printed from the
     * platform layer. Hand the value over instead. */
    { extern void sl_fs_note(unsigned af, int n);
      sl_fs_note((unsigned) g_AudioFrameCount, (int) info->frameSamples); }
    { extern void sl_acmd_capture(const void *list, int ncmds, unsigned af, unsigned slot);
      sl_acmd_capture(g_AudioManager.cmdList[g_CurrentAcmdList],
                      (int)(((s32)cmdlp - (s32)g_AudioManager.cmdList[g_CurrentAcmdList]) >> 3),
                      (unsigned) g_AudioFrameCount, (unsigned) g_CurrentAcmdList); }
#endif

    /* paranoia */
    info->task.next = 0;
    info->task.flags = 0;

    /* reply to when finished */
    info->task.msgQ = (void *) (&(g_AudioManager.replyMessageQueue.mtqueue));

    /* reply with this message */
    info->task.msg = info;
    info->task.flags = OS_SC_NEEDS_RSP;
    info->task.list.t.data_ptr = (u64*)(g_AudioManager.cmdList[g_CurrentAcmdList]);
    info->task.list.t.data_size = (((s32)cmdlp - (s32)g_AudioManager.cmdList[g_CurrentAcmdList]) >> 3) * sizeof(Acmd);
    info->task.list.t.type = M_AUDTASK;
    info->task.list.t.ucode_boot = (u64*)rspbootTextStart;
    info->task.list.t.ucode_boot_size = ((s32)gsp3DTextStart - (s32)rspbootTextStart);
    info->task.list.t.flags = 0; // 1c
    info->task.list.t.ucode = (u64*)aspMainTextStart;
    info->task.list.t.ucode_data = (u64*)aspMainDataStart;
    info->task.list.t.ucode_data_size = SP_UCODE_DATA_SIZE;
    info->task.list.t.yield_data_ptr = NULL; // 50
    info->task.list.t.yield_data_size = 0; // 54

    osSendMesg(osScGetCmdQ(&os_scheduler), (OSMesg)&info->task, OS_MESG_NOBLOCK);

    /* swap which acmd list you use each frame */
    g_CurrentAcmdList ^= 1;
}


/**
 * 2FE4	700023E4
 * Based on method
 *     static void __amHandleDoneMsg(AudioInfo *info)
 * from the n64devkit demos_old/simple/audiomgr.c.
 *
 * original documentation:
 * Really just debugging info in this frame. Checks
 * to make sure we completed before we were out of samples.
 *
 * @param info Unused.
 */
void amHandleDoneMessage(AudioInfo *info)
{
    s32 samplesLeft;
    /*
    * in the audiomgr example, firstTime is declared here with
    * the static keyword. That breaks the build, but the following
    * code will compile to a matching binary,
    */
    int *b;

    samplesLeft = (s32)osAiGetLength() >> 2;

    /*
    * The initial code probably looked like the following (and this
    * is what you get with mips_to_c):
    *
    *     if (samplesLeft == 0 && !firstTime)
    */
    b = &g_FirstTime;
    if (!samplesLeft && !(*b))
    {
        // debug printf from audioMgr demo
#ifdef ENABLE_LOG
      osSyncPrintf("audio: ai out of samples\n");
#endif
        g_FirstTime = 0;
    }
}

/**
 * 3024 70002424
 * Looks to be based on method
 *     s32 __amDMA(s32 addr, s32 len, void *state)
 * from the n64devkit.
 *
 *  original documentation:
 * This routine handles the dma'ing of samples from rom to ram.
 * First it checks the current buffers to see if the samples needed are
 * already in place. Because buffers are linked sequentially by the
 * addresses where the samples are on rom, it doesn't need to check all
 * of them, only up to the address that it needs. If it finds one, it
 * returns the address of that buffer. If it doesn't find the samples
 * that it needs, it will initiate a DMA of the samples that it needs.
 * In either case, it updates the lastFrame variable, to indicate that
 * this buffer was last used in this frame. This is important for the
 * __clearAudioDMA routine.
 *
 * @param addr ?.
 * @param len ?.
 * @param state unused.
 * @return result from call to osVirtualToPhysical
 */
s32 amDmaCallback(s32 addr, s32 len, void* state)
{
    void *freeBuffer;
    s32 delta;
    DMABuffer *dmaPtr;
    s32 addrEnd;
    s32 buffEnd;
    DMABuffer *lastDmaPtr;

    lastDmaPtr = NULL;
    dmaPtr = g_DmaState.firstUsed;
    delta = addr & 0x1;
    addrEnd = addr + len;

    /* first check to see if a currently existing buffer contains the
       sample that you need.  */
    while (dmaPtr)
    {
        buffEnd = dmaPtr->startAddr + AUDIO_DMA_MAX_BUFFER_LENGTH;

        /* since buffers are ordered */
        /* abort if past possible */
        if ((u32)dmaPtr->startAddr > (u32)addr)
        {
            break;
        }
        /* yes, found a buffer with samples */
        else if (addrEnd <= buffEnd)
        {
            /* mark it used */
            dmaPtr->lastFrame = (s32) g_AudioFrameCount;
            freeBuffer = (dmaPtr->ptr + addr) - dmaPtr->startAddr;
            return osVirtualToPhysical(freeBuffer);
        }

        lastDmaPtr = dmaPtr;
        dmaPtr = (DMABuffer*)dmaPtr->node.next;
    }

    /* get here, and you didn't find a buffer, so dma a new one */
    /* get a buffer from the free list */
    dmaPtr = g_DmaState.firstFree;

    /*
     * if you get here and dmaPtr is null, send back a bogus
     * pointer, it's better than nothing
     */
    if (!dmaPtr)
    {
        if (!lastDmaPtr)
        {
            lastDmaPtr = g_DmaState.firstUsed;
        }

        return osVirtualToPhysical(lastDmaPtr->ptr) + delta;
    }

    g_DmaState.firstFree = (DMABuffer*)dmaPtr->node.next;
    alUnlink((ALLink*)dmaPtr);

    /* add it to the used list */
    /* if you have other dmabuffers used, add this one */
    /* to the list, after the last one checked above */
    if (lastDmaPtr)
    {
        alLink((ALLink*)dmaPtr, (ALLink*)lastDmaPtr);
    }
    /* if this buffer is before any others */
    // Jam at begining of list
    else if (g_DmaState.firstUsed)
    {
        lastDmaPtr = g_DmaState.firstUsed;
        g_DmaState.firstUsed = dmaPtr;
        dmaPtr->node.next = (ALLink*)lastDmaPtr;
        dmaPtr->node.prev = 0;
        lastDmaPtr->node.prev = (ALLink*)dmaPtr;
    }
    /* no buffers in list, this is the first one */
    else
    {
        g_DmaState.firstUsed = dmaPtr;
        dmaPtr->node.next = 0;
        dmaPtr->node.prev = 0;
    }

    freeBuffer = dmaPtr->ptr;
    addr -= delta;
    dmaPtr->startAddr = addr;
    dmaPtr->lastFrame = g_AudioFrameCount;

    osPiStartDma(&g_DmaIOMessageBuffer[g_NextDMa++], OS_MESG_PRI_HIGH, OS_READ, (u32)addr, freeBuffer, AUDIO_DMA_MAX_BUFFER_LENGTH, &g_DmaMessageQueue);
    return (s32)osVirtualToPhysical(freeBuffer) + delta;
}

/**
 * 31D8 700025D8
 * Based on method
 *     ALDMAproc __amDmaNew(AMDMAState **state)
 * from the n64devkit demos_old/simple/audiomgr.c.
 *
 * original documentation:
 * Initialize the dma buffers and return the address of the
 * procedure that will be used to dma the samples from rom to ram. This
 * routine will be called once for each physical voice that is created.
 * In this case, because we know where all the buffers are, and since
 * they are not attached to a specific voice, we will only really do any
 * initialization the first time. After that we just return the address
 * to the dma routine.
 *
 * @param state will point to g_DmaState after call.
 * @return Address of dma callback function.
 */
ALDMAproc amDmaNew(DMAState** state)
{
    if (g_DmaState.u.initialized == 0)
    {
        g_DmaState.firstUsed = NULL;
        g_DmaState.firstFree = g_DmaBuffers;
        g_DmaState.u.initialized = (u8)1U;
    }

    *state = &g_DmaState;
    return &amDmaCallback;
}

/**
 * 3210 70002610
 * Based on method
 *     static void __clearAudioDMA(void)
 * from the n64devkit demos_old/simple/audiomgr.c.
 *
 * original documentation:
 * Routine to move dma buffers back to the unused list.
 * First clear out your dma messageQ. Then check each buffer to see when
 * it was last used. If that was more than FRAME_LAG frames ago, move it
 * back to the unused list.
 */
void amClearDmaBuffers(void)
{
    u32 i;
    OSMesg osmesg;
    DMABuffer *dmaPtr, *nextPtr;

    osmesg = 0;

   /*
    * Don't block here. If dma's aren't complete, you've had an audio
    * overrun. (Bad news, but go for it anyway, and try and recover.
    */
   for (i=0; i < g_NextDMa; i++)
   {
       if (osRecvMesg(&g_DmaMessageQueue, (OSMesg *)&osmesg, OS_MESG_NOBLOCK) == -1)
       {
#ifdef ENABLE_LOG
	        osSyncPrintf("Dma not done\n");
#endif
       }

#ifdef DEBUG
    /* debug logging from audioMgr.c, I think this requires #include <ultralog.h>
    * //    if (logging)
    * //        osLogEvent(log, 17, 2, osmesg->devAddr, osmesg->size);
    */
#endif
   }

    dmaPtr = g_DmaState.firstUsed;
    while (dmaPtr)
    {
        nextPtr = (DMABuffer*)dmaPtr->node.next;

        /* remove old dma's from list */
        /* Can change FRAME_LAG value.  Should be at least one.  */
        /* Larger values mean more buffers needed, but fewer DMA's */
        if (dmaPtr->lastFrame + 1 < g_AudioFrameCount)
        {
            if (g_DmaState.firstUsed == dmaPtr)
            {
                g_DmaState.firstUsed = (DMABuffer*)dmaPtr->node.next;
            }

            alUnlink((ALLink*)dmaPtr);

            if (g_DmaState.firstFree)
            {
                alLink((ALLink*)dmaPtr, (ALLink*)g_DmaState.firstFree);
            }
            else
            {
                g_DmaState.firstFree = dmaPtr;
                dmaPtr->node.next = 0;
                dmaPtr->node.prev = 0;
            }
        }
        dmaPtr = nextPtr;
    }

    g_NextDMa = 0U;
    g_AudioFrameCount = (s32)(g_AudioFrameCount + 1);
    /* POSITIVE CONTROL. The only normal writer of this counter, unconditional
     * and one per audio task - so a valid log carries exactly one event per
     * task with strictly increasing values. */
#ifdef SIGHTLINE_AUDIO_EVENTS
    SL_AEV(SL_AEV_FRAMECOUNT, g_AudioFrameCount, 0, 0, 0, 0, 0, 0);
#endif
}

