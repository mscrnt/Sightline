/* sl_precise_wait.c - wait until a CLOCK_MONOTONIC instant, to well under a
 * millisecond, without busy-waiting most of the time.
 *
 * WHY. The frame pump's pacing sleep (sl_ultra_shim.c, sl_pace_frame) is
 * nanosleep, and on this toolchain nanosleep is winpthreads', which waits on
 * the system tick whatever timeBeginPeriod says. Measured 2026-10-07 on the
 * development host (200 waits each):
 *
 *     nanosleep 1 ms / 5 ms        overshoot mean 14.6 / 10.6 ms, max 29.8 ms
 *       ... after timeBeginPeriod(1)   mean 14.6 / 10.6 ms, max 16.8 ms
 *     nanosleep 0.5 ms             returns at once (rounded down to nothing)
 *     Sleep 1 / 5 ms, period 1     mean 0.37 / 0.43 ms, max 1.5 ms
 *     high-resolution waitable timer 1 / 5 ms   mean 0.38 / 0.29 ms, max 1.1 ms
 *
 * so a nanosleep-paced frame is released anywhere up to a tick late, which is
 * what the regular presents' 8.8 / 25 ms p10 / p90 intervals at a nominal
 * 16.7 ms show (docs/backlog.md, B-HR1). The accepted pacing keeps nanosleep
 * - its frames and its timing are the default and stay exactly as they were
 * - and the high-refresh presentation path, which has to place frames inside
 * one VI, waits here instead.
 *
 * HOW. A high-resolution waitable timer (CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
 * Windows 10 1803 and later) to within SL_WAIT_SPIN of the instant, then a
 * yielding spin for the rest. Without that timer: timeBeginPeriod(1) and Sleep,
 * the same way. Elsewhere: nanosleep and the spin. */
#include <time.h>
#include <errno.h>
#ifdef _WIN32
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0600
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600     /* CreateWaitableTimerExW */
#endif
#include <windows.h>
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
#endif

#define SL_WAIT_SPIN 0.0013     /* s left to the spin: above the timer's measured worst overshoot */

static double sl_wait_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double) ts.tv_sec + (double) ts.tv_nsec * 1e-9;
}

void sl_precise_wait_until(double t);
void sl_precise_wait_until(double t)
{
#ifdef _WIN32
    static int state;           /* 0 untried, 1 hi-res timer, 2 Sleep at 1 ms */
    static HANDLE timer;
    if (state == 0) {
        timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        if (timer != NULL) state = 1;
        else {
            /* winmm is not on the link line: resolved at run time */
            typedef unsigned (WINAPI *pfn_tbp)(unsigned);
            HMODULE mm = LoadLibraryA("winmm.dll");
            pfn_tbp tbp = mm ? (pfn_tbp) GetProcAddress(mm, "timeBeginPeriod") : NULL;
            if (tbp) tbp(1);
            state = 2;
        }
    }
#endif
    for (;;) {
        double d = t - sl_wait_now();
        if (d <= 0.0) return;
        if (d > SL_WAIT_SPIN) {
            double w = d - SL_WAIT_SPIN;
#ifdef _WIN32
            if (state == 1) {
                LARGE_INTEGER due;
                due.QuadPart = -(LONGLONG) (w * 1e7);      /* 100 ns units, relative */
                if (SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE))
                    WaitForSingleObject(timer, INFINITE);
            } else {
                Sleep((DWORD) (w * 1000.0));
            }
#else
            struct timespec req;
            req.tv_sec = (long) w;
            req.tv_nsec = (long) ((w - (double) req.tv_sec) * 1e9);
            while (nanosleep(&req, &req) != 0 && errno == EINTR)
                ;
#endif
        } else {
#ifdef _WIN32
            SwitchToThread();
#endif
        }
    }
}
