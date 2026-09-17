/* Win32 runtime fills.
 *
 * Symbols the decomp references that glibc supplies on Linux and the mingw
 * CRT does not. Every one of these was, before this file existed, satisfied
 * by the build's auto-generated `long NAME(void) { return 0; }` stub - which
 * links cleanly and is WRONG at runtime in a way nothing announces. That is
 * the failure mode this codebase keeps producing (B-030: _Printf linked,
 * returned 0, and blanked every formatted string in the game; it was
 * mis-diagnosed as a HUD problem twice).
 *
 * A no-op bcopy is worse still: src/fr.c copies the video settings through
 * it and src/game/ob.c copies object payloads, so the silent failure is a
 * memory copy that simply does not happen.
 *
 * The whole file is Win32-only. On Linux none of it is compiled and the C
 * library keeps providing these, so the matching build cannot see it either
 * way - nothing here is in the matching build's translation set at all.
 */
#ifdef _WIN32

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/* The decomp translation units compile against the N64 SDK include tree,
 * which has no <stdio.h>, so their `stderr` resolves to an undefined OBJECT
 * symbol rather than to the CRT's macro. Six files reference it (bg.c,
 * bgfog.c, chrprop.c, explosion.c, padhalllv.c, propobj.c).
 *
 * It cannot be a function stub: `fprintf(stderr, ...)` passes the symbol's
 * ADDRESS as a FILE*, so a callable stub is never called - it is dereferenced
 * as a wild pointer instead, and a loud stub would stay silent while
 * corrupting. It has to be a real FILE* holding the CRT's own stderr.
 *
 * mingw32 spells `stderr` as a function call, not a link-time constant, so it
 * cannot be a static initializer. Captured before the #undef and assigned by
 * a constructor, which runs before main(). */
static FILE *sl_crt_stderr(void) { return stderr; }
#undef stderr
FILE *stderr;

__attribute__((constructor))
static void sl_win32_rt_init(void)
{
    stderr = sl_crt_stderr();
}

/* BSD order - source FIRST, destination second, the opposite of memcpy.
 * Prototyped in include/PR/os.h as (const void *, void *, int). memmove
 * rather than memcpy because bcopy is defined to tolerate overlap. */
void bcopy(const void *src, void *dst, int n)
{
    if (n > 0)
        memmove(dst, src, (size_t) n);
}

void bzero(void *dst, int n)
{
    if (n > 0)
        memset(dst, 0, (size_t) n);
}

/* libultra's formatter, declared in src/libultra/libc/xstdio.h as
 *   int _Printf(outfun prout, u8 *arg, const u8 *fmt, va_list args);
 * with outfun = char *(*)(char *, const char *, size_t). The definition in
 * src/libultrare/libc/xprintf.c is not in the native build's source glob, so
 * the reference from src/rmon.c (osSyncPrintf and friends) arrives here.
 *
 * Note this is NOT the B-030 path: native sprintf() was rerouted to vsprintf
 * in src/sprintf.c and no longer goes through _Printf at all. This covers
 * rmon.c's diagnostic output only.
 *
 * Returns the character count. Rare's own call site in src/sprintf.c returns
 * `written - 1` to its callers, but that adjustment lives at the call site
 * and is preserved there; _Printf itself reports the count it produced. */
int _Printf(char *(*prout)(char *, const char *, size_t),
            char *arg, const char *fmt, va_list args)
{
    char buf[1024];
    int n = vsnprintf(buf, sizeof buf, fmt, args);

    if (n < 0)
        return -1;
    if (n > (int) (sizeof buf - 1))     /* truncated: report what we emit */
        n = (int) (sizeof buf - 1);
    if (prout != NULL)
        prout(arg, buf, (size_t) n);
    return n;
}

#endif /* _WIN32 */
