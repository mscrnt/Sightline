/* Deterministic fixture for the AI queue's SOURCE-DERIVED refusal rule.
 *
 * WHY THIS EXISTS. On the facility fixture the queue never reaches depth 2 at
 * submit time - measured directly, `depth_before` is 1 for 447 of 448 submits
 * and 0 for the first - so the refusal branch is never taken and the cartridge
 * cannot validate it. That leaves the rule implemented but untested, which is
 * exactly the state a counter alone does not fix.
 *
 * WHAT IT VALIDATES, and what it does not. It validates that the IMPLEMENTATION
 * obeys the rule the sources state:
 *   include/PR/rcp.h:607        the address/length registers are double
 *                               buffered - writable twice before full
 *   include/PR/rcp.h:625, io/ai.c   AI_STATUS_FIFO_FULL is "addr & len buffer
 *                               full", and __osAiDeviceBusy tests exactly it
 *   libultrare/io/aisetnextbuf.c:30  osAiSetNextBuffer returns -1 BEFORE
 *                               writing either register, so a refused submit
 *                               changes nothing
 * It does NOT claim the facility exercises hardware refusal, and it is not
 * evidence about the cartridge. It is a test of this code against those lines.
 *
 * Sequence: empty -> accept -> accept -> full -> third REFUSED with state
 * unchanged -> drain one -> accept again.
 *
 * The fixture is challenged: run with SL_AI_NOREFUSE=1, which is the
 * deliberately wrong condition, and it MUST fail. A fixture that passes under
 * both is not testing anything.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

unsigned int sl_audio_set_frequency(unsigned int rate);
unsigned int sl_audio_get_length(void);
int          sl_audio_submit(void *buf, unsigned int len);
void         sl_audio_ai_retrace(void);
extern unsigned long sl_ai_refused, sl_ai_underruns, sl_ai_submits;

/* sl_audio.c asks the window layer whether a device is wanted; there is no
 * window here, so no device opens and the queue model runs alone. */
int sl_gfx_active(void) { return 0; }

static int fails;
static void check(const char *what, int ok)
{
    printf("  %-46s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) fails++;
}

int main(void)
{
    static short buf[8192];
    unsigned int len = 6000;          /* > one retrace drain, so a single
                                       * retrace cannot empty an entry */
    unsigned int l_before, l_after;
    unsigned long refused_before;
    int r1, r2, r3, r4;
    int i;

    memset(buf, 0, sizeof buf);
    sl_audio_set_frequency(22050);

    printf("AI queue refusal fixture (source-derived rule)\n");
    check("empty queue reports length 0", sl_audio_get_length() == 0);

    r1 = sl_audio_submit(buf, len);
    check("first submit accepted", r1 == 0);
    r2 = sl_audio_submit(buf, len);
    check("second submit accepted (double buffered)", r2 == 0);

    l_before = sl_audio_get_length();
    refused_before = sl_ai_refused;
    r3 = sl_audio_submit(buf, len);
    l_after = sl_audio_get_length();

    check("third submit REFUSED with -1", r3 == -1);
    check("refusal left the queue state unchanged", l_after == l_before);
    check("refusal counted exactly once", sl_ai_refused == refused_before + 1);

    /* drain until one entry has been consumed - at 22047 Hz a retrace takes
     * about 1470 bytes, so a 6000-byte entry needs five. Bounded so a broken
     * drain cannot spin. */
    for (i = 0; i < 5; i++) sl_audio_ai_retrace();

    r4 = sl_audio_submit(buf, len);
    check("after draining one entry, a submit is accepted", r4 == 0);

    printf("%s (%d failure%s)\n", fails ? "FIXTURE FAILED" : "fixture passed",
           fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
