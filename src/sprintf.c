#include <stdarg.h>
#include <ultra64.h>
/*#include <stddef.h>*/



char *proutSprintf(char *dst, const char *src, size_t count) {
    return (char *) memcpy((u8 *) dst, (u8 *) src, count) + count;
}

#ifdef __sgi
int sprintf(char *dst, const char *fmt, ...) {
    s32 written;
    va_list args;
    va_start(args, fmt);
    written = _Printf(proutSprintf, dst, fmt, args);
    if (written >= 0) {
        dst[written] = 0;
    }
    return written-1;
}
#else
/*
 * Native sprintf. MEASURED 2026-08-25: the libultra _Printf above is linked
 * into the native binary and returns 0 for every call, so every sprintf on
 * this platform writes a NUL at index 0 and nothing else. Confirmed at
 * gunfire.c:5936 with a correct value (93) and a correct format ("%d")
 * producing buffer == "".
 *
 * That is 106 call sites tree-wide, and it is why the ammo readout draws six
 * commands and no glyphs (B-030): gunDrawHudInteger formats into an empty
 * buffer and gunDrawHudString then has no characters to emit. It is also why
 * the first sdl_shot() silently wrote to an empty path.
 *
 * The matching build keeps Rare's implementation untouched above, so it stays
 * byte-identical - this branch cannot be reached under IDO. Rule 5 is not in
 * play: the intended output is unchanged, it is the platform that was failing
 * to produce it. The odd `written - 1` return is preserved deliberately rather
 * than "corrected", because it is the convention the call sites were written
 * against.
 *
 * vsprintf is declared here rather than pulled from <stdio.h> on purpose: this
 * file compiles with the N64 SDK include path, which shadows the host headers.
 */
extern int vsprintf(char *, const char *, va_list);

int sprintf(char *dst, const char *fmt, ...) {
    s32 written;
    va_list args;
    va_start(args, fmt);
    written = vsprintf(dst, fmt, args);
    va_end(args);
    return written-1;
}
#endif




