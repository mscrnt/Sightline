/**
 * Credits-roll witness, native side. Inert unless SL_CREDITS_DBG is set.
 *
 * WHAT THIS IS FOR. The Cuba credits (bondviewRenderCredits, bondview2.c)
 * lay every row out from the setup file's CreditsEntry table: two text ids,
 * an x position and an alignment per column, with -1 meaning "as the row
 * before". A screenshot shows where the rows LANDED; it cannot say whether
 * the table was read wrongly or a correct table was drawn wrongly. This
 * prints both halves of that question from the game's own values:
 *
 *   SL_CREDITS_DBG=1       the table as it sits in memory, once, on the
 *                          first frame credits_pointer is set (the first 16
 *                          records: id1 id2 pos1 al1 pos2 al2, and the text
 *                          each id names); then, while the roll runs, every
 *                          SL_CREDITS_EVERY frames (default 60) the roll
 *                          counter, the view rectangle, and for every row
 *                          the roll would draw this frame the x/y the
 *                          layout arithmetic gives it - the same expressions
 *                          bondviewRenderCredits evaluates, over the same
 *                          inputs, including the carried xpos/align state.
 *
 * WHY NATIVE, NOT src/game. Same argument as sl_mission_dbg.c: src/game
 * stays byte-identical to the matching build; src/native is compiled with
 * the game include path and included by nothing in src/game. READ-ONLY.
 * The arithmetic below is a transcription of bondviewRenderCredits, not a
 * second implementation the game uses - it exists so a log line can name
 * the row's computed coordinates without touching the routine.
 */
#ifndef __sgi

#include <ultra64.h>
#include <bondgame.h>
#include <boss.h>
#include <fr.h>
#include "bondview.h"
#include "textrelated.h"
#include "language.h"

extern int    fprintf(void *, const char *, ...);
extern void  *stderr;
extern char  *getenv(const char *);
extern int    atoi(const char *);

static int g_cd_checked, g_cd_on, g_cd_every;
static int g_cd_dumped;
static s32 g_cd_last_state = -1;

static void sl_cd_text(s32 id, char *out, int n)
{
    const u8 *s = (id == 0) ? NULL : langGet(id);
    int i = 0;
    if (s != NULL) {
        while (*s != '\0' && i < n - 1) {
            if (*s >= 0x20 && *s < 0x7f) out[i++] = (char) *s;
            s++;
        }
    }
    out[i] = '\0';
}

static void sl_cd_dump_table(s32 frame)
{
    s32 i;
    char t1[40], t2[40];

    fprintf(stderr, "sl_credits: f%d table at %p (stage %d): idx id1 id2 pos1 al1 pos2 al2\n",
            (int) frame, (void *) credits_pointer, (int) bossGetStageNum());
    for (i = 0; i < 16; i++) {
        CreditsEntry *e = &credits_pointer[i];
        sl_cd_text(e->TextId1, t1, (int) sizeof t1);
        sl_cd_text(e->TextId2, t2, (int) sizeof t2);
        fprintf(stderr, "sl_credits:   [%2d] %04x %04x %6d %6d %6d %6d  \"%s\" | \"%s\"\n",
                (int) i, (unsigned) e->TextId1, (unsigned) e->TextId2,
                (int) e->Position1, (int) (s16) e->Alignment1,
                (int) e->Position2, (int) (s16) e->Alignment2, t1, t2);
        if (e->TextId1 == 0 && e->TextId2 == 0)
            break;
    }
}

/* One column of one row, as bondviewRenderCredits places it. */
static void sl_cd_row(s32 frame, s32 i, s32 col, s32 id, s32 xpos, s32 align, s32 roll)
{
    s32 th = 0, tw = 0, x, x2, y;
    char t[40];
    u8 *text = langGet(id);

    y = ((viGetViewTop() + (i * 16)) - roll) + viGetViewHeight();
    textMeasure(&th, &tw, (char *) text, ptrFontZurichBoldChars, ptrFontZurichBold, 0);
    if (align == CREDITS_ALIGN_LEFT)        { x = xpos - tw;        x2 = xpos; }
    else if (align == CREDITS_ALIGN_CENTER) { x = xpos - (tw >> 1); x2 = x + tw; }
    else                                    { x = xpos;             x2 = xpos + tw; }
    sl_cd_text(id, t, (int) sizeof t);
    fprintf(stderr, "sl_credits:   f%d row %2d col %d id %04x xpos %3d align %2d tw %3d th %2d -> x %4d..%4d y %4d \"%s\"\n",
            (int) frame, (int) i, (int) col, (unsigned) id, (int) xpos, (int) align,
            (int) tw, (int) th, (int) x, (int) x2, (int) y, t);
}

void sl_credits_probe_frame(s32 frame);
void sl_credits_probe_frame(s32 frame)
{
    s32 roll, start, end, i;
    s32 xpos1, xpos2, align1, align2;

    if (!g_cd_checked) {
        const char *e = getenv("SL_CREDITS_DBG");
        g_cd_checked = 1;
        g_cd_on = (e != NULL && *e != '\0' && *e != '0');
        e = getenv("SL_CREDITS_EVERY");
        g_cd_every = (e != NULL) ? atoi(e) : 60;
        if (g_cd_every <= 0) g_cd_every = 60;
    }
    if (!g_cd_on)
        return;

    if (credits_state != g_cd_last_state) {
        fprintf(stderr, "sl_credits: f%d credits_state %d -> %d roll %d\n",
                (int) frame, (int) g_cd_last_state, (int) credits_state, (int) camera_80036438);
        g_cd_last_state = credits_state;
    }
    if (credits_pointer == NULL)
        return;
    if (!g_cd_dumped) {
        g_cd_dumped = 1;
        sl_cd_dump_table(frame);
    }
    if (credits_state != 1 || (frame % g_cd_every) != 0)
        return;

    /* bondviewRenderCredits, transcribed: the roll counter has already been
     * advanced for this frame by the time this probe runs. */
    roll = camera_80036438;
    xpos1 = 0xdc; xpos2 = 0xdc;
    align1 = CREDITS_ALIGN_RIGHT; align2 = CREDITS_ALIGN_RIGHT;
    start = (roll - viGetViewHeight()) / 16;
    end = (roll / 16) + 1;
    if (start < 0) start = 0;
    fprintf(stderr, "sl_credits: f%d roll %d view left %d top %d w %d h %d fb %dx%d rows [%d,%d)\n",
            (int) frame, (int) roll, (int) viGetViewLeft(), (int) viGetViewTop(),
            (int) viGetViewWidth(), (int) viGetViewHeight(), (int) viGetX(), (int) viGetY(),
            (int) start, (int) end);
    for (i = 0; i < start; i++) {
        CreditsEntry *e = &credits_pointer[i];
        if (e->TextId1 == 0 && e->TextId2 == 0) { end = i; start = i; break; }
        if (e->TextId1 != 0x5011) {
            if (e->Position1 >= 0) xpos1 = e->Position1;
            if ((s16) e->Alignment1 >= CREDITS_ALIGN_RIGHT) align1 = (s16) e->Alignment1;
        }
        if (e->TextId2 != 0x5011) {
            if (e->Position2 >= 0) xpos2 = e->Position2;
            if ((s16) e->Alignment2 >= CREDITS_ALIGN_RIGHT) align2 = (s16) e->Alignment2;
        }
    }
    for (i = start; i < end && (credits_pointer[i].TextId1 || credits_pointer[i].TextId2); i++) {
        CreditsEntry *e = &credits_pointer[i];
        if (e->TextId1 != 0x5011) {
            if (e->Position1 >= 0) xpos1 = e->Position1;
            if ((s16) e->Alignment1 >= CREDITS_ALIGN_RIGHT) align1 = (s16) e->Alignment1;
            sl_cd_row(frame, i, 1, e->TextId1, xpos1, align1, roll);
        }
        if (e->TextId2 != 0x5011) {
            if (e->Position2 >= 0) xpos2 = e->Position2;
            if ((s16) e->Alignment2 >= CREDITS_ALIGN_RIGHT) align2 = (s16) e->Alignment2;
            sl_cd_row(frame, i, 2, e->TextId2, xpos2, align2, roll);
        }
    }
}
#endif
