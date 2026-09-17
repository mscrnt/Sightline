#include <ultra64.h>
#include "frametiming.h"

// data
s32 lastFrameCounter = -1;
s32 currentFrameCounter = 0;

/**
 * Appears to be rendered framerate, or some kind of counter since the last frame update.
 */
s32 speedgraphframes = 1;

#if defined(BUGFIX_R1)
// EU address D_8004111C
f32 jpD_800484CC = 1.0f;

// EU address D_80041120
f32 jpD_800484D0 = 1.0f;
#endif

s32 previousFrameCounter = -1;
s32 halfFrameCounter = 0; // half of currentFrameCounter
s32 isFrameCounterOdd = 0; // is currentFrameCounter Odd
s32 halfMinusPreviousCounter = 0; // half - previousFrameCounter
u32 copy_of_osgetcount_value_0 = 0;
u32 copy_of_osgetcount_value_1 = 0;
s32 frameDelay = 1; //usually 1



/**
 * Stores the current OS count in the two global variables.
 */
void store_osgetcount(void)
{
    copy_of_osgetcount_value_1 = osGetCount();
    copy_of_osgetcount_value_0 = copy_of_osgetcount_value_1;
}


/**
 * Updates the timing-related counters and frame information based on the given argument.
 *
 * @param deltaFrames The number of frames to add to the current frame counter.
 */
void updateFrameCounters(s32 deltaFrames)
{
    copy_of_osgetcount_value_0 = (s32) copy_of_osgetcount_value_1;
    copy_of_osgetcount_value_1 = osGetCount();

    lastFrameCounter = currentFrameCounter;
    currentFrameCounter = (s32) (currentFrameCounter + deltaFrames);
    speedgraphframes = deltaFrames;

    #ifdef BUGFIX_R1
    jpD_800484CC = (f32) deltaFrames;
    #ifdef REFRESH_PAL
    jpD_800484D0 = (jpD_800484CC * 60.0f) / 50.0f;
    #else
    jpD_800484D0 = (f32) jpD_800484CC;
    #endif
    #endif

    previousFrameCounter = (s32) halfFrameCounter;
    halfFrameCounter = (s32) (currentFrameCounter / 2);
    isFrameCounterOdd = (s32) (currentFrameCounter & 1);
    halfMinusPreviousCounter = (s32) (halfFrameCounter - previousFrameCounter);
}


/**
 * Waits until the appropriate time has passed before updating the frame counters.
 * This function effectively controls the frame rate by waiting for the next tick.
 */
void waitForNextFrame(void) //maybe WaitForTick
{
#ifdef __sgi
  u32 nextFrameTime; //next frame time?
  
  do {
    #ifdef REFRESH_PAL
    nextFrameTime = ((osGetCount() - copy_of_osgetcount_value_1) + 465525) / 931050; 
    #else
    nextFrameTime = ((osGetCount() - copy_of_osgetcount_value_1) + 387937) / 775875; //current time + 1/5
    #endif
  } while (nextFrameTime < frameDelay);

  frameDelay = 1;
  updateFrameCounters(nextFrameTime);
#else
  /* Sightline native: this busy-wait on osGetCount is THE point where real
   * time entered the simulation - the exact mechanism Phase 0 existed to
   * remove. Under fixed-step time the wait IS the frame boundary: advance
   * the deterministic clock by one frame and account exactly one tick.
   * frameDelay's accumulated value still feeds through so slowdown logic
   * behaves as on hardware. */
  extern void sl_frame_advance(void);
  extern u32 sl_ticks_next(void);
  u32 nextFrameTime;
  sl_frame_advance();
  sl_ticks_next();   /* recorded VI count for this frame enters the clock */
  /* the original elapsed->ticks formula over the synthetic clock: this is
   * how load time (injected via SL_BOOT_ELAPSED_FRAMES to mirror the
   * emulator's physically-elapsed VIs) reaches the frame counters and
   * every timer derived from them */
  nextFrameTime = ((osGetCount() - copy_of_osgetcount_value_1) + 387937) / 775875;
  if (nextFrameTime < frameDelay)
      nextFrameTime = frameDelay;
  frameDelay = 1;
  updateFrameCounters(nextFrameTime);
#endif
}


void setFrameDelay(s32 arg0) {
    #ifdef LEFTOVERDEBUG
    frameDelay = arg0;
    #endif
}

#ifdef VERSION_EU
void eu_sub_7f0c00a4(void)
{
  
}
#endif




