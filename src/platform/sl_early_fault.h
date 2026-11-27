/* ---- THE OPT-IN EARLY FIRST-CHANCE FAULT OBSERVER (SL_EARLY_FAULT_DIAG) ---
 *
 * WHAT GAP THIS ADDRESSES, measured rather than assumed. src/platform/sl_main.c
 * opens the crash log at :2292 and registers the product's access-violation
 * handler, AddVectoredExceptionHandler(1, sl_win_veh), only at :2303 - after
 * sl_gfx_init() and sl_gfx_begin_frame() at :2298-2299. A fault anywhere in
 * that interval therefore reaches no handler of this program's, and the
 * already-open report stays zero bytes. That is a REPORTING gap. It is not a
 * diagnosis: it says why a report could be empty, not what faulted or where.
 *
 * WHAT THIS IS. A diagnostic observer registered in exactly that interval -
 * after the log is open, before graphics init - and REMOVED immediately before
 * the product's handler is registered, so the two never coexist. It records a
 * bounded number of first-chance exceptions and ALWAYS returns
 * EXCEPTION_CONTINUE_SEARCH.
 *
 * WHAT IT MUST NEVER DO, each forbidden for a specific reason:
 *
 *   NOT HANDLE. It never returns EXCEPTION_CONTINUE_EXECUTION, never retries an
 *   instruction and never reclassifies a code. With the observer armed, the
 *   disposition of every exception in the process is bit-for-bit what it was
 *   without it; the observer can only add lines to a log. That is the whole
 *   safety argument, and it is why this can be armed in an interval where no
 *   handler of ours exists at all.
 *
 *   NOT PAGE. It never calls sl_pager_fault, and the product's pager-aware
 *   handler is NOT moved earlier to cover this interval. Moving it would be
 *   unsafe, measurably: sl_pager_init() runs at sl_main.c:2339, LATER than even
 *   the normal handler, and sl_pager_fault (src/platform/sl_ultra_shim.c:3217)
 *   calls sl_rom_load() for any address inside the kseg2 window - file I/O and
 *   allocation - before it discovers the window was never reserved. Running
 *   that from a first-chance handler during graphics init would put stdio and
 *   the allocator on the fault path, in a process that has not yet reserved the
 *   window those addresses belong to. An observer that only observes has no
 *   such dependency and no such risk.
 *
 *   NOT TAKE THE LOADER LOCK. No GetModuleHandle, GetModuleHandleEx,
 *   GetModuleFileName, LoadLibrary or GetProcAddress, so no module NAME is
 *   resolved - only an allocation base and an offset from it. The reason is
 *   sharper here than on the normal fault path: the whole point of this
 *   interval is that it covers GL initialisation, which LOADS THE DRIVER'S ICD.
 *   A first-chance exception raised while a thread holds the loader lock -
 *   precisely what a fault inside LoadLibrary would be - met by a handler that
 *   asks the loader a question is a deadlock, and a deadlock here would convert
 *   a crash into a hang and destroy the evidence this exists to capture. The
 *   same rule is already enforced for the product's handler by
 *   tools/windows/crashreport-grammar.py (check A5, LOADER_CALLS); this file
 *   holds to it for a stronger reason.
 *
 *   NOT RUN BY DEFAULT. Absent selector: no registration call, no line, no
 *   state. See the grammar below.
 *
 * WHAT A RECORDED EVENT IS AND IS NOT. FIRST-CHANCE means observed before any
 * handler has had the chance to handle it, so a recorded exception may be
 * entirely benign - drivers and C++ runtimes raise and handle exceptions as
 * ordinary control flow. Every line therefore says FIRST-CHANCE, and no line
 * ever says "crash", "fault at" or anything else that would let a handled
 * driver exception be read as the terminal fault. An observed event is evidence
 * that an exception of that code occurred at that address on that thread. It is
 * not evidence that the process died of it, and the absence of observed events
 * is not evidence that nothing went wrong.
 */
#ifndef SL_EARLY_FAULT_H
#define SL_EARLY_FAULT_H

/* ---- THE SELECTOR'S GRAMMAR ---------------------------------------------
 *
 * SL_EARLY_FAULT_DIAG, a NEW name of its own. Deliberately NOT the GL-debug
 * selector: that one is spent, it arms a different subsystem, and a reader must
 * be able to tell from a launch which diagnostic was asked for. The grammar is
 * the same exact one, for the same reasons given in src/gfx/sl_gfx_gldebug.h:
 * "1" arms, unset / "" / "0" are silent, EVERYTHING else is malformed and
 * refuses rather than guessing. The value itself is never printed; the verdict
 * is named and the text that produced it is not. */
enum {
    SL_EFD_SEL_ABSENT    = 0,  /* unset: nothing printed, nothing armed */
    SL_EFD_SEL_OFF       = 1,  /* "0" or "": an explicit off, equally silent */
    SL_EFD_SEL_ARMED     = 2,  /* exactly "1" */
    SL_EFD_SEL_MALFORMED = 3   /* anything else: refused, never armed */
};

/* Pure: no I/O, no state, no platform call. `raw` is a getenv result, NULL
 * included. The getenv itself happens in the caller, never here and never in
 * the handler. */
int sl_early_fault_parse(const char *raw);

/* ---- THE PLATFORM, INJECTED --------------------------------------------
 *
 * This module calls no Win32 function directly; every one arrives here. That is
 * what lets the observer, its filter, its bounds, its labelling and its
 * registration order be driven on a CPU with no exception, no driver and no
 * registered handler at all - see tools/profiling/native/test_sl_early_fault.c.
 *
 *   add_veh      AddVectoredExceptionHandler(1, handler) - FIRST in the chain,
 *                which is the only position that observes an exception before
 *                anything else can handle it. Returns the handle, or NULL on
 *                failure.
 *   remove_veh   RemoveVectoredExceptionHandler(handle). Non-zero on success,
 *                which is the Win32 convention and is checked rather than
 *                assumed.
 *   current_tid  GetCurrentThreadId. A TEB read: no lock, no allocation. A
 *                VEH is PROCESS-WIDE, so this handler sees faults on every
 *                thread, including threads the GL driver creates during
 *                initialisation - which is exactly the coverage the prior
 *                captures say is needed, their faults having been on a
 *                non-main thread.
 *   region       VirtualQuery, reduced to the two fields this observer will
 *                report: the allocation base containing `addr` and the region
 *                state. 0 on success. NO module name is derived from it: see
 *                the loader-lock paragraph above. A failure is reported, not
 *                hidden, and never stops the code/eip/addr from being recorded.
 *   emit         ONE line, newline included by the caller's sink. In the
 *                product this is the EXISTING proven write path (sl_say in
 *                sl_main.c: write(2) and write(crash fd), no stdio, no
 *                allocation, no locks) rather than a second implementation of
 *                it.
 */
struct sl_early_fault_host {
    void *(*add_veh)(void *handler);
    int   (*remove_veh)(void *handle);
    unsigned long (*current_tid)(void);
    int   (*region)(unsigned long addr, unsigned long *alloc,
                    unsigned long *state);
    void  (*emit)(const char *line);
};

/* AFTER sl_crash_open, BEFORE sl_gfx_init. Parses the selector; on an exact "1"
 * registers the observer first in the VEH chain and says so. Returns non-zero
 * only when a handle came back. A malformed value is refused with its reason
 * and the value withheld; an Add that fails is refused and reported, and
 * nothing is left to remove. Absent or explicitly off: NO platform call and no
 * line at all.
 *
 * `main_tid` / `main_tid_known` let every recorded line say whether the
 * observing thread is the main one, which is the distinction the prior captures
 * turned on. They are read once here, not queried on the fault path. */
int sl_early_fault_arm(const char *raw, const struct sl_early_fault_host *h,
                       unsigned long main_tid, int main_tid_known);

/* IMMEDIATELY BEFORE AddVectoredExceptionHandler(1, sl_win_veh). The two must
 * never coexist, and the reason is concrete rather than tidiness: once the
 * pager is live, the game demand-pages the cartridge by TAKING ACCESS
 * VIOLATIONS inside the kseg2 window, which sl_pager_fault handles and
 * continues. An observer still armed would record each of those as a
 * first-chance event, bury the one interesting line under routine paging and
 * add a log write to a hot path.
 *
 * Returns non-zero when the handler was removed. A removal that FAILS is
 * reported and the observer is LEFT IN PLACE - it is pass-through, so leaving
 * it cannot change any exception's disposition, whereas pretending it is gone
 * would make every later line a lie. Reports a summary of what was observed. A
 * no-op when nothing was armed. */
int sl_early_fault_disarm(const struct sl_early_fault_host *h);

/* ---- BOUNDS -------------------------------------------------------------
 *
 * SL_EFD_EVENT_CAP events per process, then ONE suppressed line and silence.
 * A first-chance observer is on the hottest path a process has: C++ runtimes
 * and GL drivers raise exceptions as control flow, and an uncapped observer
 * would turn graphics init into megabytes of log and hide the first event -
 * the one that matters - behind the thousandth. The line buffer is fixed and
 * on the stack, there is no recursion, and nothing is allocated.
 *
 * SL_EFD_LINE_MAX IS A MEASUREMENT, NOT A ROUND NUMBER. The longest line this
 * module can emit - an access violation with every field resolved - is 293
 * characters, and at the 256 bytes this was first written with it was silently
 * TRUNCATED: the tail clauses and the terminating newline were lost, so a
 * recorded event ran into the next line of the log and the statement that no
 * module name was resolved disappeared. The offline suite caught it, which is
 * the only reason it is not in a capture. 448 leaves room for the longest line
 * plus every wording change short of a new field, and the fixture asserts the
 * longest line is not truncated so the bound cannot quietly become wrong again.
 * The cost is 448 bytes of stack in a handler that recurses nowhere. */
#define SL_EFD_EVENT_CAP 8
#define SL_EFD_LINE_MAX  448

/* ---- THE FILTER, as a RULE rather than a list -------------------------
 *
 * Recorded only when BOTH hold of the NTSTATUS-shaped exception code:
 *
 *   severity is ERROR          (code & 0xC0000000) == 0xC0000000
 *   and it is not customer-defined   (code & 0x20000000) == 0
 *
 * A rule rather than an allow-list because the set of codes a driver stack can
 * raise is not knowable here, while the two bits that say "this is an error the
 * system defined" are. What it admits and excludes, by example:
 *
 *   ADMITTED  0xC0000005 ACCESS_VIOLATION, 0xC000001D ILLEGAL_INSTRUCTION,
 *             0xC0000094 INT_DIVIDE_BY_ZERO, 0xC0000096 PRIV_INSTRUCTION,
 *             0xC00000FD STACK_OVERFLOW, 0xC0000374 HEAP_CORRUPTION
 *   EXCLUDED  0x40010006 DBG_PRINTEXCEPTION_C (OutputDebugString) and
 *             0x4001000A, both INFORMATIONAL severity; 0x80000003 BREAKPOINT,
 *             WARNING severity; 0x406D1388, the thread-name exception;
 *             0xE06D7363, the Microsoft C++ exception - ERROR severity but
 *             CUSTOMER-defined, which is what the second bit test is for and
 *             is the one a severity test alone would wrongly admit.
 *
 * A NON-ADMITTED CODE IS NOT A FAULT AND PRODUCES NO LINE AT ALL. Treating
 * routine driver-init exception control flow as a fault is the specific
 * misreporting this filter exists to prevent. The fault ADDRESS is recorded
 * only for 0xC0000005, because ExceptionInformation[0] and [1] are defined for
 * that code and for nothing else here. */
int sl_early_fault_admits(unsigned long code);

#ifdef _WIN32
/* The product's real platform: AddVectoredExceptionHandler,
 * RemoveVectoredExceptionHandler, GetCurrentThreadId, VirtualQuery and the
 * emit sink, wired up in src/platform/sl_early_fault.c so that the only thing
 * src/platform/sl_main.c needs from this diagnostic is two calls and the
 * three-line emit definition below.
 *
 * sl_early_fault_emit is DEFINED BY sl_main.c, which owns the already-open
 * crash fd and the proven write path (sl_say). It is declared here so the
 * dependency is visible from both sides. It carries no string literal of its
 * own, so nothing of this diagnostic's text enters sl_main.c - which is what
 * keeps the sl_say-literal extraction in tools/windows/crashreport-grammar.py
 * measuring exactly the set of reasons it measured before. */
const struct sl_early_fault_host *sl_early_fault_platform(void);
void sl_early_fault_emit(const char *s);
#endif

#endif /* SL_EARLY_FAULT_H */
