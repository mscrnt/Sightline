/* The opt-in early first-chance fault observer (SL_EARLY_FAULT_DIAG).
 *
 * Every contract, the selector's grammar, the filter rule, the bounds and the
 * reasons this observer may not handle, page or ask the loader anything are
 * documented in src/platform/sl_early_fault.h. This file holds the decisions.
 *
 * <windows.h> IS INCLUDED FOR TYPES ONLY. EXCEPTION_POINTERS,
 * EXCEPTION_RECORD, CONTEXT and the CALLBACK convention cannot be spelled
 * without it, and the handler's signature has to be exactly the one
 * AddVectoredExceptionHandler expects or the stack is wrong. NO Win32 FUNCTION
 * IS CALLED FROM THIS FILE except through struct sl_early_fault_host, which is
 * what lets the whole module be driven on a CPU; a reader can check that claim
 * by searching this file for a call that is not through `h` or `s_plat`.
 *
 * NO <stdio.h>, NO <stdlib.h>, NO <string.h>. The observer runs inside a
 * first-chance exception, possibly on a driver thread, possibly while another
 * thread holds a lock it knows nothing about. stdio takes locks, the allocator
 * takes locks, and either one on this path can turn a crash into a hang. Lines
 * are assembled with the bounded appenders below into a fixed stack buffer and
 * handed to one injected sink. The whole file depends on no library at all.
 */
#include "sl_early_fault.h"
#ifdef _WIN32
#include <windows.h>
#endif

/* ---- MODULE STATE -------------------------------------------------------
 *
 * All zero in a launch that did not arm the selector, and the whole cost of
 * that launch is one integer test in sl_early_fault_arm.
 *
 * NO ATOMICS, AND WHY THAT IS HONEST RATHER THAN CONVENIENT. A VEH is
 * process-wide, so two threads can enter the observer at once and the counter
 * below is then racy: a concurrent pair could both read the same count. The
 * consequence is bounded and stated rather than papered over - the cap may
 * admit a few more or a few fewer lines than SL_EFD_EVENT_CAP, and two lines
 * may interleave in the log, which the existing reports already show happening
 * between the fault writer and an unrelated banner. What is NOT at risk is
 * safety: every write is a single bounded sink call, no state is read back to
 * make a decision, and nothing is freed. An interlocked counter was considered
 * and rejected because it adds a lock-prefixed read-modify-write to a path that
 * may be inside the loader, to tighten a bound that is a log-volume policy and
 * not a correctness property.
 *
 * `s_plat` IS NOT NAMED s_host, AND THE NAME IS LOAD-BEARING. MEASURED on this
 * toolchain: <windows.h> reaches <winsock.h>, which reaches <inaddr.h>, where
 * line 26 is `#define s_host S_un.S_un_b.s_b2` - one of the in_addr field
 * macros. A static called s_host in any file that includes <windows.h> is
 * therefore textually rewritten, and the compiler reports an undeclared `S_un`
 * at every use, which reads like a missing header rather than a name clash. The
 * other in_addr macros to avoid for the same reason are s_addr, s_net, s_imp,
 * s_impno and s_lh. */
static const struct sl_early_fault_host *s_plat;
static void         *s_handle;        /* what add_veh returned */
static int           s_armed;
static unsigned long s_events;        /* admitted events seen this process */
static unsigned long s_said;          /* lines emitted, for the cap */
static unsigned long s_skipped;       /* admitted but past the cap */
static unsigned long s_ignored;       /* not admitted by the filter */
static unsigned long s_main_tid;
static int           s_main_tid_known;

int sl_early_fault_parse(const char *raw)
{
    if (raw == NULL) return SL_EFD_SEL_ABSENT;
    if (raw[0] == '\0') return SL_EFD_SEL_OFF;
    if (raw[0] == '1' && raw[1] == '\0') return SL_EFD_SEL_ARMED;
    if (raw[0] == '0' && raw[1] == '\0') return SL_EFD_SEL_OFF;
    return SL_EFD_SEL_MALFORMED;
}

int sl_early_fault_admits(unsigned long code)
{
    /* The two bits, and nothing else. See the header for what this admits and
     * excludes and why a severity test alone would wrongly admit the Microsoft
     * C++ exception. */
    if ((code & 0xC0000000ul) != 0xC0000000ul) return 0;
    if ((code & 0x20000000ul) != 0ul) return 0;
    return 1;
}

/* ---- BOUNDED APPENDERS -------------------------------------------------
 *
 * Always NUL-terminating, never writing beyond outsz-1, no library call. The
 * same shape as the formatter in src/gfx/sl_gfx_gldebug.c and for one extra
 * reason here: this code runs in a context where even snprintf's locale and
 * lock behaviour is more than can be justified. */
static int efd_app(char *out, int outsz, int n, const char *s)
{
    while (*s != '\0' && n < outsz - 1) out[n++] = *s++;
    out[n] = '\0';
    return n;
}

/* Fixed eight nibbles, like sl_say_hex in sl_main.c: a constant width is what
 * makes two lines comparable by eye in a log read under pressure. */
static int efd_app_hex(char *out, int outsz, int n, unsigned long v)
{
    static const char hx[] = "0123456789abcdef";
    int sh;

    n = efd_app(out, outsz, n, "0x");
    for (sh = 28; sh >= 0; sh -= 4) {
        if (n >= outsz - 1) break;
        out[n++] = hx[(v >> sh) & 0xful];
    }
    out[n] = '\0';
    return n;
}

static int efd_app_dec(char *out, int outsz, int n, unsigned long v)
{
    char d[24];
    int i = 0;

    do { d[i++] = (char) ('0' + (int) (v % 10ul)); v /= 10ul; } while (v != 0ul && i < 23);
    while (i > 0 && n < outsz - 1) out[n++] = d[--i];
    out[n] = '\0';
    return n;
}

static void efd_say(const struct sl_early_fault_host *h, const char *line)
{
    if (h != NULL && h->emit != NULL) h->emit(line);
}

/* ---- THE OBSERVATION ----------------------------------------------------
 *
 * One line, and it opens with the label. FIRST-CHANCE appears before any value
 * so that a line read in isolation, or a line truncated by a log cut, still
 * says what it is. Nothing in it uses the words crash or fault for the event
 * itself: a handled driver exception must not be readable as the terminal
 * fault, and the only defence against that is the wording. */
static void efd_record(unsigned long code, unsigned long eip, int have_info,
                       unsigned long kind, unsigned long addr)
{
    const struct sl_early_fault_host *h = s_plat;
    char line[SL_EFD_LINE_MAX];
    unsigned long tid = 0, alloc = 0, state = 0;
    int have_region = 0, n;

    if (h == NULL) return;
    if (h->current_tid != NULL) tid = h->current_tid();
    /* VirtualQuery for the allocation base only - never a module name, never
     * the loader. Its failure is reported in the line, not hidden, and does not
     * stop the code, eip and address from being recorded: those three are the
     * fields that cannot fail, so they are gathered before anything that can. */
    if (h->region != NULL && h->region(eip, &alloc, &state) == 0) have_region = 1;

    n = efd_app(line, (int) sizeof line, 0,
                "sightline early-fault: FIRST-CHANCE observed (not necessarily"
                " terminal) #");
    n = efd_app_dec(line, (int) sizeof line, n, s_events);
    n = efd_app(line, (int) sizeof line, n, " code ");
    n = efd_app_hex(line, (int) sizeof line, n, code);
    n = efd_app(line, (int) sizeof line, n, " eip ");
    n = efd_app_hex(line, (int) sizeof line, n, eip);
    n = efd_app(line, (int) sizeof line, n, " tid ");
    n = efd_app_hex(line, (int) sizeof line, n, tid);
    n = efd_app(line, (int) sizeof line, n,
                !s_main_tid_known ? " main-thread unknown" :
                tid == s_main_tid ? " on the main thread" :
                                    " NOT on the main thread");
    if (have_info) {
        /* Defined for 0xC0000005 and for nothing else admitted here. */
        n = efd_app(line, (int) sizeof line, n,
                    kind == 0ul ? " access READ addr " :
                    kind == 1ul ? " access WRITE addr " :
                    kind == 8ul ? " access EXECUTE addr " :
                                  " access (unrecognised) addr ");
        n = efd_app_hex(line, (int) sizeof line, n, addr);
    } else {
        n = efd_app(line, (int) sizeof line, n,
                    " (no access/address information for this code)");
    }
    if (have_region) {
        n = efd_app(line, (int) sizeof line, n, " eip-alloc-base ");
        n = efd_app_hex(line, (int) sizeof line, n, alloc);
        n = efd_app(line, (int) sizeof line, n, " eip-offset ");
        n = efd_app_hex(line, (int) sizeof line, n, eip - alloc);
        n = efd_app(line, (int) sizeof line, n, " region-state ");
        n = efd_app_hex(line, (int) sizeof line, n, state);
    } else {
        /* An explicit reason, never an absent field: a field that is simply
         * missing reads as "there was nothing there", which is a different
         * claim from "the query did not answer". */
        n = efd_app(line, (int) sizeof line, n,
                    " eip region unresolved - VirtualQuery gave no answer");
    }
    /* NO MODULE NAME, said out loud, because its absence is otherwise
     * indistinguishable from a lookup that failed. */
    (void) efd_app(line, (int) sizeof line, n,
                   " (no module name: the loader is never called here)\n");
    efd_say(h, line);
}

#ifdef _WIN32
/* The handler itself.
 *
 * ALWAYS EXCEPTION_CONTINUE_SEARCH. There is no path through this function
 * that returns anything else, and that is the property every other safety
 * claim rests on: with the observer armed, the disposition of every exception
 * in this process is exactly what it would have been without it. */
static LONG CALLBACK efd_veh(EXCEPTION_POINTERS *ep)
{
    unsigned long code, eip = 0, kind = 0, addr = 0;
    int have_info = 0;

    if (s_plat == NULL) return EXCEPTION_CONTINUE_SEARCH;
    /* A FORGED, NULL OR PARTIAL POINTER SET IS SURVIVED, NOT TRUSTED. Windows
     * supplies both records in practice; this costs two tests and removes a
     * class of second fault inside the instrument, which is the failure the
     * product's own handler was once destroyed by (sl_main.c's B-063 note). */
    if (ep == NULL || ep->ExceptionRecord == NULL) {
        if (s_said < (unsigned long) SL_EFD_EVENT_CAP) {
            s_said++;
            efd_say(s_plat, "sightline early-fault: FIRST-CHANCE observed (not"
                            " necessarily terminal) - the exception record was"
                            " not readable, so nothing about it is recorded\n");
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }
    code = (unsigned long) ep->ExceptionRecord->ExceptionCode;
    if (!sl_early_fault_admits(code)) {
        /* Counted, never reported. Driver and C++ runtime control flow lives
         * here and a line per occurrence would drown the log. */
        s_ignored++;
        return EXCEPTION_CONTINUE_SEARCH;
    }
    s_events++;
    if (s_said >= (unsigned long) SL_EFD_EVENT_CAP) {
        if (s_skipped == 0ul)
            efd_say(s_plat, "sightline early-fault: FIRST-CHANCE observed (not"
                            " necessarily terminal) - the observer reached its"
                            " per-process cap; further events are counted and"
                            " not printed\n");
        s_skipped++;
        return EXCEPTION_CONTINUE_SEARCH;
    }
    s_said++;
    if (ep->ContextRecord != NULL)
#if defined(_M_IX86) || defined(__i386__)
        eip = (unsigned long) ep->ContextRecord->Eip;
#else
        eip = (unsigned long) ep->ContextRecord->Rip;
#endif
    if (code == 0xC0000005ul && ep->ExceptionRecord->NumberParameters >= 2) {
        kind = (unsigned long) ep->ExceptionRecord->ExceptionInformation[0];
        addr = (unsigned long) ep->ExceptionRecord->ExceptionInformation[1];
        have_info = 1;
    }
    efd_record(code, eip, have_info, kind, addr);
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif /* _WIN32 */

int sl_early_fault_arm(const char *raw, const struct sl_early_fault_host *h,
                       unsigned long main_tid, int main_tid_known)
{
    int sel = sl_early_fault_parse(raw);

    s_armed = 0;
    s_handle = NULL;
    s_plat = NULL;
    if (sel == SL_EFD_SEL_ABSENT || sel == SL_EFD_SEL_OFF) return 0;
    if (sel == SL_EFD_SEL_MALFORMED) {
        /* The verdict, not the value. */
        efd_say(h, "sightline early-fault: SL_EARLY_FAULT_DIAG is set to a"
                   " malformed value - the early first-chance observer stays"
                   " OFF (the only value that arms it is 1)\n");
        return 0;
    }
    if (h == NULL || h->add_veh == NULL || h->emit == NULL) {
        efd_say(h, "sightline early-fault: SL_EARLY_FAULT_DIAG=1 - REFUSED,"
                   " this build cannot register a vectored handler\n");
        return 0;
    }
    s_events = 0; s_said = 0; s_skipped = 0; s_ignored = 0;
    s_main_tid = main_tid;
    s_main_tid_known = main_tid_known;
    /* The host is published BEFORE the handler exists, because the instant
     * add_veh returns the handler may already be running on another thread. */
    s_plat = h;
#ifdef _WIN32
    s_handle = h->add_veh((void *) efd_veh);
#else
    s_handle = NULL;
#endif
    if (s_handle == NULL) {
        /* Refused and reported, and nothing is left behind to remove: a later
         * disarm must not be able to believe it has a handler to take out. */
        s_plat = NULL;
        efd_say(h, "sightline early-fault: SL_EARLY_FAULT_DIAG=1 - REFUSED,"
                   " AddVectoredExceptionHandler returned no handle; no"
                   " observer is registered\n");
        return 0;
    }
    s_armed = 1;
    efd_say(h, "sightline early-fault: SL_EARLY_FAULT_DIAG=1 - an early"
               " FIRST-CHANCE observer is armed first in the vectored chain"
               " for the interval between the crash log opening and the"
               " product handler's registration. It only observes and always"
               " returns CONTINUE_SEARCH, so no exception's disposition"
               " changes; a recorded event is FIRST-CHANCE and not necessarily"
               " terminal, and no recorded event is not evidence that nothing"
               " went wrong\n");
    return 1;
}

int sl_early_fault_disarm(const struct sl_early_fault_host *h)
{
    char line[SL_EFD_LINE_MAX];
    int removed = 0, n;

    if (!s_armed) return 0;             /* the default launch never gets here */
    if (h != NULL && h->remove_veh != NULL && s_handle != NULL)
        removed = (h->remove_veh(s_handle) != 0);

    /* THE SUMMARY IS EMITTED WHETHER OR NOT THE REMOVAL WORKED, and says which
     * happened. The counts are the whole observation: zero admitted events in
     * this interval is itself a finding, and so is a non-zero ignored count,
     * which says the filter was exercised rather than never reached. */
    n = efd_app(line, (int) sizeof line, 0,
                "sightline early-fault: observer ");
    n = efd_app(line, (int) sizeof line, n,
                removed ? "REMOVED before the product handler is registered"
                        : "could NOT be removed (RemoveVectoredExceptionHandler"
                          " failed); it is left in place, which cannot change"
                          " any exception's disposition because it only"
                          " observes");
    n = efd_app(line, (int) sizeof line, n, " - admitted ");
    n = efd_app_dec(line, (int) sizeof line, n, s_events);
    n = efd_app(line, (int) sizeof line, n, ", printed ");
    n = efd_app_dec(line, (int) sizeof line, n, s_said);
    n = efd_app(line, (int) sizeof line, n, ", past the cap ");
    n = efd_app_dec(line, (int) sizeof line, n, s_skipped);
    n = efd_app(line, (int) sizeof line, n, ", filtered out ");
    n = efd_app_dec(line, (int) sizeof line, n, s_ignored);
    (void) efd_app(line, (int) sizeof line, n,
                   " (FIRST-CHANCE counts; none of them is a claim about what"
                   " ended the process)\n");
    efd_say(h, line);

    if (removed) {
        /* Only now: while the handler may still run, the host it writes
         * through must stay valid. */
        s_plat = NULL;
        s_handle = NULL;
        s_armed = 0;
    }
    return removed;
}

#ifdef _WIN32
/* ---- THE PRODUCT'S PLATFORM, in one place -------------------------------
 *
 * The real Win32 calls, each a thin wrapper so the module above holds none of
 * them. `sl_early_fault_emit` is DEFINED IN src/platform/sl_main.c and routes
 * to sl_say - the write(2)-plus-crash-fd path that is already the product's
 * proven async-safe sink - rather than a second implementation of the same
 * thing in this file. No string literal of this diagnostic lives in sl_main.c:
 * every line above is assembled here, which also keeps them out of reach of the
 * sl_say-literal extraction in tools/windows/crashreport-grammar.py. */
void sl_early_fault_emit(const char *s);

static void *efd_add(void *handler)
{
    return (void *) AddVectoredExceptionHandler(1ul, (PVECTORED_EXCEPTION_HANDLER) handler);
}

static int efd_remove(void *handle)
{
    return (int) RemoveVectoredExceptionHandler(handle);
}

static unsigned long efd_tid(void)
{
    return (unsigned long) GetCurrentThreadId();
}

static int efd_region(unsigned long addr, unsigned long *alloc,
                      unsigned long *state)
{
    MEMORY_BASIC_INFORMATION mbi;

    if (VirtualQuery((const void *) addr, &mbi, sizeof mbi) != sizeof mbi)
        return -1;
    *alloc = (unsigned long) (ULONG_PTR) mbi.AllocationBase;
    *state = (unsigned long) mbi.State;
    return 0;
}

static const struct sl_early_fault_host s_platform = {
    efd_add, efd_remove, efd_tid, efd_region, sl_early_fault_emit
};

const struct sl_early_fault_host *sl_early_fault_platform(void);
const struct sl_early_fault_host *sl_early_fault_platform(void)
{
    return &s_platform;
}
#endif /* _WIN32 */
