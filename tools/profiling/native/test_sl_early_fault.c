/* Offline tests for the opt-in early first-chance fault observer
 * (src/platform/sl_early_fault.c).
 *
 * WHAT IS REAL. The module's own text: this file #includes
 * src/platform/sl_early_fault.c, so every decision under test is the product's,
 * including its file statics - which is the only way the per-process event cap
 * and the arm/disarm lifecycle can be driven at all, and it means the product
 * carries no test hook and no second compilation mode. The REAL <windows.h> is
 * included too, so EXCEPTION_RECORD, CONTEXT, EXCEPTION_POINTERS and the
 * EXCEPTION_CONTINUE_SEARCH constant are Windows's own rather than this file's,
 * and the handler driven below is the one with the real CALLBACK signature.
 *
 * WHAT IS FAKE. Every Win32 call the module makes, because it makes all of them
 * through struct sl_early_fault_host. AddVectoredExceptionHandler,
 * RemoveVectoredExceptionHandler, GetCurrentThreadId and VirtualQuery are
 * functions in this file that record what they were asked and answer what a
 * scenario chose. NOTHING IS REGISTERED WITH WINDOWS: no vectored handler is
 * ever installed by this process, no exception is ever raised, and the handler
 * is invoked as an ordinary function call on forged records.
 *
 * WHAT THESE TESTS THEREFORE DO NOT ESTABLISH, and it is the larger half:
 *   - nothing about real exception delivery. Whether Windows calls a
 *     first-chance handler where this one expects to be called, what a driver
 *     raises during GL init, and whether a write from inside a real
 *     first-chance exception completes are all unmeasured here;
 *   - nothing about the crash under investigation. An observer that would
 *     record an event proves nothing about what ended the r765c2 process, and
 *     a design that passes these tests is not a diagnosis;
 *   - nothing about deadlock. The loader-lock hazard is avoided by NOT CALLING
 *     the loader, which is a structural property checked by reading the source
 *     (tools/windows/earlyfaulttest.ps1 scans for it); it is not demonstrated
 *     by running anything here.
 *
 * Usage: test_sl_early_fault.exe <mode>
 *   modes: parse, filter, arm, observe, cap, label, lifecycle, default, forged
 * Exits 0 when the mode passes, 1 otherwise. One mode per process, because the
 * cap counter and the armed state have process lifetime by construction.
 */
#include <windows.h>

#include <stdio.h>
#include <string.h>

/* The module's own text. Its statics are this file's to reset between
 * scenarios, which is what makes the per-process properties drivable. */
#include "../../src/platform/sl_early_fault.c"

static int g_pass, g_fail;

#define CHECK(cond, ...) do {                                                 \
        if (cond) { g_pass++; }                                               \
        else {                                                                \
            g_fail++;                                                         \
            printf("FAIL: ");                                                 \
            printf(__VA_ARGS__);                                              \
            printf(" (line %d)\n", __LINE__);                                 \
        }                                                                     \
    } while (0)

/* THE PAGER MUST NEVER BE REACHED. src/platform/sl_ultra_shim.c's real
 * sl_pager_fault calls sl_rom_load() - file I/O and allocation - for any
 * address inside the kseg2 window, and sl_pager_init has not even run at the
 * point this observer is armed. The product module does not declare it, so this
 * definition is what a call would have to resolve to; the counter staying zero
 * after every scenario is the run-time half of that claim, and the source scan
 * in the harness script is the structural half. */
static long g_pager_calls;
int sl_pager_fault(unsigned long addr);
int sl_pager_fault(unsigned long addr)
{
    (void) addr;
    g_pager_calls++;
    return 0;
}

/* THE SINK sl_main.c OWNS. The product's host struct names
 * sl_early_fault_emit, which src/platform/sl_main.c defines as a three-line
 * route into sl_say; this file is not linked against sl_main.c, so it stands in
 * for that symbol. It routes into the recording sink below, which means that if
 * any scenario ever reached the PRODUCT host by mistake instead of the fake
 * one, the line would still be captured here rather than vanishing - a silent
 * write to a real fd is exactly the failure a stub returning void would hide.
 * The counter makes that visible: no mode may move it. */
static long g_product_sink_calls;
static void fake_emit(const char *line);
void sl_early_fault_emit(const char *s)
{
    g_product_sink_calls++;
    fake_emit(s);
}

/* ---- THE RECORDING FAKE PLATFORM ---------------------------------------- */

#define SAY_MAX   64
#define ORDER_MAX 64
/* Wider than the module's own SL_EFD_LINE_MAX, and wider than the longest
 * fixed line it emits. THE CAPTURE MUST NOT BE THE THING THAT TRUNCATES: at
 * 400 this buffer cut the arm line short, which made the label assertion fail
 * for a reason that had nothing to do with the module - a fixture artefact
 * reported as a product defect is the worst kind of test failure. */
#define LINE_KEEP 800

static struct {
    /* answers a scenario chooses */
    int  add_fails;               /* AddVectoredExceptionHandler -> NULL      */
    int  remove_fails;            /* RemoveVectoredExceptionHandler -> 0      */
    int  region_fails;            /* VirtualQuery gives no answer             */
    unsigned long region_alloc, region_state;
    unsigned long tid;            /* what GetCurrentThreadId answers          */

    /* what happened */
    long calls;                   /* every platform call except emit          */
    long adds, removes, tids, regions;
    void *last_handle;
    char  order[ORDER_MAX][16];   /* "add", "remove", "normal-add"            */
    int   n_order;
    char  said[SAY_MAX][LINE_KEEP];
    int   n_said;
    void *handler_given;          /* what add_veh was handed                  */
} fk;

static void note(const char *what)
{
    if (fk.n_order < ORDER_MAX) {
        strncpy(fk.order[fk.n_order], what, sizeof fk.order[0] - 1);
        fk.order[fk.n_order][sizeof fk.order[0] - 1] = '\0';
        fk.n_order++;
    }
}

static void fake_reset(void)
{
    memset(&fk, 0, sizeof fk);
    fk.tid = 0x4321u;
    fk.region_alloc = 0x64880000ul;
    fk.region_state = 0x1000ul;
    g_pager_calls = 0;
    /* and the module's process state, which this file owns because it compiled
     * the module's text */
    s_plat = NULL; s_handle = NULL; s_armed = 0;
    s_events = 0; s_said = 0; s_skipped = 0; s_ignored = 0;
    s_main_tid = 0; s_main_tid_known = 0;
}

static void *fake_add(void *handler)
{
    fk.calls++; fk.adds++;
    note("add");
    fk.handler_given = handler;
    if (fk.add_fails) return NULL;
    fk.last_handle = (void *) 0xABCD1234ul;
    return fk.last_handle;
}

static int fake_remove(void *handle)
{
    fk.calls++; fk.removes++;
    note("remove");
    if (fk.remove_fails) return 0;
    return handle == fk.last_handle;
}

static unsigned long fake_tid(void)
{
    fk.calls++; fk.tids++;
    return fk.tid;
}

static int fake_region(unsigned long addr, unsigned long *alloc,
                       unsigned long *state)
{
    fk.calls++; fk.regions++;
    (void) addr;
    if (fk.region_fails) return -1;
    *alloc = fk.region_alloc;
    *state = fk.region_state;
    return 0;
}

static void fake_emit(const char *line)
{
    if (fk.n_said < SAY_MAX) {
        strncpy(fk.said[fk.n_said], line, LINE_KEEP - 1);
        fk.said[fk.n_said][LINE_KEEP - 1] = '\0';
        fk.n_said++;
    }
}

static const struct sl_early_fault_host HOST = {
    fake_add, fake_remove, fake_tid, fake_region, fake_emit
};

/* Stands in for sl_main.c's AddVectoredExceptionHandler(1, sl_win_veh) so the
 * ORDER the product establishes - arm, then any events, then disarm, then the
 * product handler - is one assertion rather than a reading of the diff. */
static void normal_handler_add(void)
{
    note("normal-add");
}

static int said_has(const char *needle)
{
    int i;
    for (i = 0; i < fk.n_said; i++)
        if (strstr(fk.said[i], needle) != NULL) return 1;
    return 0;
}

static int said_count(const char *needle)
{
    int i, n = 0;
    for (i = 0; i < fk.n_said; i++)
        if (strstr(fk.said[i], needle) != NULL) n++;
    return n;
}

/* EVERY emitted line must carry the label and must end in a newline: the sink
 * adds nothing, and a line without the label could be read as a terminal
 * fault report. The summary line is the one exception to the label and is
 * identified by its own text, so it cannot silently lose it either. */
static int all_lines_labelled(void)
{
    int i;
    for (i = 0; i < fk.n_said; i++) {
        size_t n = strlen(fk.said[i]);
        if (n == 0 || fk.said[i][n - 1] != '\n') return 0;
        if (strstr(fk.said[i], "FIRST-CHANCE") == NULL) return 0;
    }
    return 1;
}

static const char *order_str(void)
{
    static char buf[ORDER_MAX * 18];
    int i, n = 0;

    buf[0] = '\0';
    for (i = 0; i < fk.n_order && n < (int) sizeof buf - 20; i++) {
        if (i > 0) buf[n++] = ',';
        strcpy(buf + n, fk.order[i]);
        n += (int) strlen(fk.order[i]);
    }
    buf[n] = '\0';
    return buf;
}

/* ---- FORGING AN EXCEPTION ----------------------------------------------
 *
 * Windows's own records, filled by this file, handed to the product's real
 * handler as an ordinary call. Nothing is raised and nothing is registered. */
static EXCEPTION_RECORD   g_rec;
static CONTEXT            g_ctx;
static EXCEPTION_POINTERS g_ep;

static LONG fire(unsigned long code, unsigned long eip, unsigned long kind,
                 unsigned long addr, int nparams)
{
    memset(&g_rec, 0, sizeof g_rec);
    memset(&g_ctx, 0, sizeof g_ctx);
    g_rec.ExceptionCode = (DWORD) code;
    g_rec.NumberParameters = (DWORD) nparams;
    g_rec.ExceptionInformation[0] = (ULONG_PTR) kind;
    g_rec.ExceptionInformation[1] = (ULONG_PTR) addr;
    g_ctx.Eip = (DWORD) eip;
    g_ep.ExceptionRecord = &g_rec;
    g_ep.ContextRecord = &g_ctx;
    return efd_veh(&g_ep);
}

static LONG fire_av(unsigned long eip, unsigned long kind, unsigned long addr)
{
    return fire(0xC0000005ul, eip, kind, addr, 2);
}

static int arm_ok(void)
{
    return sl_early_fault_arm("1", &HOST, 0x1111ul, 1);
}

/* ======================== MODE: parse =================================== */
/* The grammar, exactly, and deliberately the same one as the GL-debug
 * selector's - a reader should not have to learn two. Every negative would arm
 * under a prefix match, an atoi or a "first character is 1" test. */
static int mode_parse(void)
{
    static const char *malformed[] = {
        "2", "01", "1 ", " 1", "1x", "11", "10", "x1", "true", "TRUE", "yes",
        "on", "ON", "1\t", "\t1", "1\n", "-1", "+1", "1.0", "0x1", "00", "0 ",
        " ", "1;1"
    };
    unsigned int i;

    CHECK(sl_early_fault_parse(NULL) == SL_EFD_SEL_ABSENT,
          "an unset variable is not ABSENT");
    CHECK(sl_early_fault_parse("") == SL_EFD_SEL_OFF,
          "an empty value is not an explicit OFF");
    CHECK(sl_early_fault_parse("1") == SL_EFD_SEL_ARMED, "\"1\" does not arm");
    CHECK(sl_early_fault_parse("0") == SL_EFD_SEL_OFF,
          "\"0\" is not an explicit OFF");
    for (i = 0; i < sizeof malformed / sizeof malformed[0]; i++)
        CHECK(sl_early_fault_parse(malformed[i]) == SL_EFD_SEL_MALFORMED,
              "value %u of the malformed set is not reported MALFORMED", i);
    printf("  parse: 1 arms, unset/empty/0 are silent, %u malformed forms"
           " refuse\n", (unsigned) (sizeof malformed / sizeof malformed[0]));
    return g_fail == 0;
}

/* ======================== MODE: filter ================================== */
/* The admission RULE. The excluded codes are the ones a GL driver and a C++
 * runtime raise as ordinary control flow during initialisation, which is
 * exactly the interval this observer is armed for - so treating one of them as
 * a fault is the specific misreporting the filter prevents. */
static int mode_filter(void)
{
    static const struct { unsigned long code; int admit; const char *what; } t[] = {
        { 0xC0000005ul, 1, "ACCESS_VIOLATION" },
        { 0xC000001Dul, 1, "ILLEGAL_INSTRUCTION" },
        { 0xC0000094ul, 1, "INT_DIVIDE_BY_ZERO" },
        { 0xC0000096ul, 1, "PRIV_INSTRUCTION" },
        { 0xC00000FDul, 1, "STACK_OVERFLOW" },
        { 0xC0000374ul, 1, "HEAP_CORRUPTION" },
        { 0x40010006ul, 0, "DBG_PRINTEXCEPTION_C (OutputDebugString)" },
        { 0x4001000Aul, 0, "DBG_PRINTEXCEPTION_WIDE_C" },
        { 0x80000003ul, 0, "BREAKPOINT" },
        { 0x80000001ul, 0, "GUARD_PAGE (warning severity)" },
        { 0x406D1388ul, 0, "the thread-name exception" },
        { 0xE06D7363ul, 0, "the Microsoft C++ exception" },
        { 0x00000000ul, 0, "a success code" },
        { 0x20000000ul, 0, "customer bit alone" },
        { 0xE0000001ul, 0, "a customer error code" }
    };
    unsigned int i;

    for (i = 0; i < sizeof t / sizeof t[0]; i++)
        CHECK(sl_early_fault_admits(t[i].code) == t[i].admit,
              "%s (0x%08lx) is %s but the filter says %s", t[i].what,
              t[i].code, t[i].admit ? "admitted" : "excluded",
              sl_early_fault_admits(t[i].code) ? "admitted" : "excluded");

    /* AND THE FILTER IS WHAT THE HANDLER OBEYS, not just a predicate nobody
     * calls: an excluded code must produce NO LINE AT ALL and must still pass
     * through. */
    fake_reset();
    CHECK(arm_ok() == 1, "arming failed");
    fk.n_said = 0;
    for (i = 0; i < sizeof t / sizeof t[0]; i++) {
        LONG r = fire(t[i].code, 0x65139441ul, 0, 4ul, 2);
        CHECK(r == EXCEPTION_CONTINUE_SEARCH,
              "%s returned %ld, not CONTINUE_SEARCH", t[i].what, (long) r);
    }
    CHECK(fk.n_said == 6,
          "the handler printed %d line(s) for 15 codes, expected 6 (one per"
          " admitted code)", fk.n_said);
    CHECK(s_ignored == 9,
          "the handler counted %lu filtered-out code(s), expected 9",
          s_ignored);
    CHECK(g_pager_calls == 0, "the pager was called %ld time(s)", g_pager_calls);
    printf("  filter: 6 admitted, 9 excluded by the two-bit rule, no line for"
           " an excluded code, every code passes through\n");
    return g_fail == 0;
}

/* ======================== MODE: arm ===================================== */
static int mode_arm(void)
{
    /* absent: no platform call, no line, no state. The default launch. */
    fake_reset();
    CHECK(sl_early_fault_arm(NULL, &HOST, 0x1111ul, 1) == 0,
          "an unset selector armed");
    CHECK(fk.calls == 0, "an unset selector made %ld platform call(s)", fk.calls);
    CHECK(fk.n_said == 0, "an unset selector printed %d line(s)", fk.n_said);

    /* explicit off: identically silent */
    fake_reset();
    CHECK(sl_early_fault_arm("0", &HOST, 0x1111ul, 1) == 0,
          "an explicit off armed");
    CHECK(fk.calls == 0 && fk.n_said == 0,
          "an explicit off made %ld call(s) and printed %d line(s)",
          fk.calls, fk.n_said);

    /* malformed: refused, said once, and the VALUE IS NOT ECHOED */
    fake_reset();
    CHECK(sl_early_fault_arm("oN", &HOST, 0x1111ul, 1) == 0,
          "a malformed selector armed");
    CHECK(fk.adds == 0, "a malformed selector registered a handler");
    CHECK(fk.n_said == 1, "a malformed selector said %d line(s), expected 1",
          fk.n_said);
    CHECK(said_has("malformed"), "the refusal does not name the verdict");
    CHECK(!said_has("oN"), "the refusal ECHOED the selector's value");

    /* armed: registered once, first in the chain, and the module's own handler
     * is what was handed over */
    fake_reset();
    CHECK(arm_ok() == 1, "an exact \"1\" did not arm");
    CHECK(fk.adds == 1, "the handler was registered %ld time(s), expected 1",
          fk.adds);
    CHECK(fk.handler_given == (void *) efd_veh,
          "the pointer registered is not the module's observer");
    CHECK(fk.n_said == 1, "arming said %d line(s), expected 1", fk.n_said);
    CHECK(said_has("FIRST-CHANCE"), "the arm line carries no FIRST-CHANCE label");
    CHECK(said_has("CONTINUE_SEARCH"),
          "the arm line does not record that the observer only observes");
    /* The arm line names the crash LOG, which is that file's name, so a blanket
     * ban on the word would fail on an accurate noun. What must never appear is
     * the product reporter's own terminal-fault wording, which is what would
     * let a reader take one of these lines for the real report. */
    CHECK(!said_has("=== sightline CRASH ===") && !said_has("ACCESS VIOLATION"),
          "the arm line reuses the product reporter's terminal-fault wording");
    CHECK(said_has("not necessarily terminal"),
          "the arm line does not disclaim what a recorded event means");

    /* AN ADD THAT FAILS IS REFUSED AND REPORTED, and leaves NOTHING to remove:
     * a later disarm must not believe it holds a handler. */
    fake_reset();
    fk.add_fails = 1;
    CHECK(sl_early_fault_arm("1", &HOST, 0x1111ul, 1) == 0,
          "a failed registration still reported armed");
    CHECK(said_has("REFUSED"), "a failed registration was not reported REFUSED");
    CHECK(s_armed == 0 && s_handle == NULL && s_plat == NULL,
          "a failed registration left module state behind");
    fk.removes = 0;
    CHECK(sl_early_fault_disarm(&HOST) == 0,
          "disarm after a failed registration claimed a removal");
    CHECK(fk.removes == 0,
          "disarm after a failed registration tried to remove %ld handler(s)",
          fk.removes);
    /* and the handler, if somehow reached, does nothing and passes through */
    fk.n_said = 0;
    CHECK(fire_av(0x1000ul, 0, 4ul) == EXCEPTION_CONTINUE_SEARCH,
          "the handler did not pass through after a failed registration");
    CHECK(fk.n_said == 0,
          "the handler printed %d line(s) with no host published", fk.n_said);

    /* a build that cannot register at all refuses rather than arming */
    {
        struct sl_early_fault_host noadd = HOST;
        fake_reset();
        noadd.add_veh = NULL;
        CHECK(sl_early_fault_arm("1", &noadd, 0x1111ul, 1) == 0,
              "a host with no registration call armed anyway");
        CHECK(said_has("REFUSED"), "that refusal was not reported");
    }
    printf("  arm: silent when unset/off, refuses malformed without echoing it,"
           " registers the module's own observer once, and a failed"
           " registration refuses and leaves nothing to remove\n");
    return g_fail == 0;
}

/* ======================== MODE: observe ================================= */
/* What one admitted event records, and the two failure shapes inside it. */
static int mode_observe(void)
{
    fake_reset();
    CHECK(arm_ok() == 1, "arming failed");
    fk.n_said = 0;
    fk.tid = 0x2222ul;                  /* not the main thread id armed with */
    CHECK(fire_av(0x65139441ul, 0, 4ul) == EXCEPTION_CONTINUE_SEARCH,
          "an access violation did not pass through");
    CHECK(fk.n_said == 1, "one event produced %d line(s)", fk.n_said);
    CHECK(said_has("FIRST-CHANCE") && said_has("not necessarily terminal"),
          "the event line is not labelled as first-chance and non-terminal");
    CHECK(said_has("0xc0000005") && said_has("0x65139441")
          && said_has("0x00002222") && said_has("0x00000004"),
          "the line is missing the code, eip, tid or fault address: \"%s\"",
          fk.n_said > 0 ? fk.said[0] : "");
    CHECK(said_has("access READ"), "the access kind is absent");
    CHECK(said_has("NOT on the main thread"),
          "the non-main thread was not reported as such");
    CHECK(said_has("eip-alloc-base 0x64880000") && said_has("eip-offset 0x008b9441"),
          "the module base and offset are absent or wrong: \"%s\"",
          fk.n_said > 0 ? fk.said[0] : "");
    CHECK(said_has("no module name"),
          "the line does not say that no module name was resolved");
    /* THE LONGEST LINE MUST NOT BE TRUNCATED. This is the one driven above -
     * an access violation with the thread, the access kind, the address, the
     * allocation base, the offset and the region state all resolved. At the
     * 256-byte bound this module was first written with, it ran to 293 and lost
     * both its tail clause and its newline, so one event ran into the next line
     * of the log. The buffer bound is not a style choice and this is what keeps
     * it honest: the line must end in a newline and must still carry its final
     * clause. */
    CHECK(fk.n_said > 0 && fk.said[0][strlen(fk.said[0]) - 1] == '\n',
          "the longest line lost its newline - it was truncated at the buffer"
          " bound (%d characters)",
          fk.n_said > 0 ? (int) strlen(fk.said[0]) : 0);
    CHECK(fk.n_said > 0
          && strlen(fk.said[0]) < (size_t) SL_EFD_LINE_MAX - 1,
          "the longest line is %d characters against a %d-byte buffer - it is"
          " at or over the bound, so the next wording change truncates it",
          fk.n_said > 0 ? (int) strlen(fk.said[0]) : 0, SL_EFD_LINE_MAX);
    CHECK(g_pager_calls == 0, "the pager was called");

    /* the main-thread case, and the unknown case, each said in its own words */
    fake_reset();
    (void) sl_early_fault_arm("1", &HOST, 0x4321ul, 1);
    fk.n_said = 0;
    fk.tid = 0x4321ul;
    (void) fire_av(0x1000ul, 1, 8ul);
    CHECK(said_has("on the main thread") && !said_has("NOT on the main thread"),
          "a main-thread event was not reported as one");
    CHECK(said_has("access WRITE"), "a write was not reported as a write");

    fake_reset();
    (void) sl_early_fault_arm("1", &HOST, 0ul, 0);   /* main tid not known */
    fk.n_said = 0;
    (void) fire_av(0x1000ul, 8ul, 0x10ul);
    CHECK(said_has("main-thread unknown"),
          "an unknown main thread was not reported as unknown");
    CHECK(said_has("access EXECUTE"),
          "an execute fault was not reported as one");

    /* A MODULE-LOOKUP FAILURE STILL RECORDS THE CODE, EIP AND ADDRESS. The
     * three fields that cannot fail are gathered before anything that can, and
     * the failure gets its own reason rather than an absent field. */
    fake_reset();
    CHECK(arm_ok() == 1, "arming failed");
    fk.region_fails = 1;
    fk.n_said = 0;
    CHECK(fire_av(0x65139441ul, 0, 4ul) == EXCEPTION_CONTINUE_SEARCH,
          "a region-query failure changed the disposition");
    CHECK(fk.n_said == 1, "it produced %d line(s)", fk.n_said);
    CHECK(said_has("0xc0000005") && said_has("0x65139441")
          && said_has("0x00000004"),
          "a region failure lost the code, eip or address: \"%s\"",
          fk.n_said > 0 ? fk.said[0] : "");
    CHECK(said_has("unresolved"),
          "a region failure printed no reason - an absent field reads as an"
          " empty region");
    CHECK(said_has("eip-alloc-base") == 0,
          "a failed region query still printed an allocation base");

    /* a host with no region query at all: same outcome, no crash */
    {
        struct sl_early_fault_host noq = HOST;
        fake_reset();
        noq.region = NULL;
        (void) sl_early_fault_arm("1", &noq, 0x1111ul, 1);
        fk.n_said = 0;
        CHECK(fire_av(0x1000ul, 0, 4ul) == EXCEPTION_CONTINUE_SEARCH,
              "a host with no region query changed the disposition");
        CHECK(said_has("unresolved"), "it printed no reason for the absence");
    }

    /* AN ADMITTED NON-AV CODE CARRIES NO ADDRESS, and says so rather than
     * printing a zero that would read as a fault at address 0. */
    fake_reset();
    CHECK(arm_ok() == 1, "arming failed");
    fk.n_said = 0;
    (void) fire(0xC000001Dul, 0x1000ul, 0, 0, 0);
    CHECK(said_has("no access/address information"),
          "a non-AV code did not say that it carries no address: \"%s\"",
          fk.n_said > 0 ? fk.said[0] : "");
    CHECK(!said_has("access READ"),
          "a non-AV code was reported with an access kind it does not have");

    /* AN AV WITH TOO FEW PARAMETERS is treated the same way: the fields are
     * read only when the record says they are there. */
    fake_reset();
    CHECK(arm_ok() == 1, "arming failed");
    fk.n_said = 0;
    (void) fire(0xC0000005ul, 0x1000ul, 0, 0x4ul, 1);
    CHECK(said_has("no access/address information"),
          "an AV record with one parameter was read as if it had two");

    printf("  observe: code, eip, tid, main-thread flag, access kind, address,"
           " allocation base and offset; a region failure keeps the"
           " unfailable fields and names its reason; no module name, no pager\n");
    return g_fail == 0;
}

/* ======================== MODE: cap ===================================== */
static int mode_cap(void)
{
    int i, caps;

    fake_reset();
    CHECK(arm_ok() == 1, "arming failed");
    fk.n_said = 0;
    for (i = 0; i < SL_EFD_EVENT_CAP + 20; i++)
        CHECK(fire_av(0x1000ul + (unsigned long) i, 0, 4ul)
              == EXCEPTION_CONTINUE_SEARCH,
              "event %d did not pass through", i);
    caps = said_count("reached its per-process cap");
    CHECK(caps == 1, "the cap was reported %d time(s), expected exactly 1", caps);
    /* CAP event lines plus exactly one suppression line. The suppression line
     * is deliberately NOT counted against the cap: it is the record that the
     * cap was reached, and a cap that consumed its own announcement would make
     * a capped run indistinguishable from an uncapped one that stopped. */
    CHECK(fk.n_said == SL_EFD_EVENT_CAP + 1,
          "the observer printed %d line(s) for %d events, expected %d event"
          " line(s) plus one suppression line", fk.n_said,
          SL_EFD_EVENT_CAP + 20, SL_EFD_EVENT_CAP);
    CHECK(said_count("not necessarily terminal") == SL_EFD_EVENT_CAP + 1,
          "only %d of %d lines carry the non-terminal disclaimer",
          said_count("not necessarily terminal"), fk.n_said);
    CHECK(s_events == (unsigned long) (SL_EFD_EVENT_CAP + 20),
          "it admitted %lu event(s), expected all %d - the cap bounds the"
          " PRINTING, not the counting", s_events, SL_EFD_EVENT_CAP + 20);
    CHECK(s_skipped == 20ul,
          "it counted %lu event(s) past the cap, expected 20", s_skipped);
    CHECK(all_lines_labelled(),
          "a line past the cap lost its FIRST-CHANCE label or its newline");
    CHECK(g_pager_calls == 0, "the pager was called");

    /* and the counts reach the summary, which is the only place a reader
     * learns that events were dropped */
    fk.n_said = 0;
    (void) sl_early_fault_disarm(&HOST);
    CHECK(said_has("past the cap"),
          "the summary does not report the dropped events: \"%s\"",
          fk.n_said > 0 ? fk.said[0] : "");
    printf("  cap: %d lines then exactly one suppressed line, every event"
           " still counted and reported in the summary\n", SL_EFD_EVENT_CAP);
    return g_fail == 0;
}

/* ======================== MODE: label ================================== */
/* The wording, as a property of every line the module can emit. A handled
 * driver exception misread as the terminal fault is the specific
 * misreporting the task forbids, and the wording is the only defence. */
static int mode_label(void)
{
    int i;

    fake_reset();
    CHECK(arm_ok() == 1, "arming failed");
    (void) fire_av(0x65139441ul, 0, 4ul);
    fk.region_fails = 1;
    (void) fire_av(0x65139441ul, 1, 4ul);
    (void) fire(0xC00000FDul, 0x2000ul, 0, 0, 0);
    (void) fire(NULL == NULL ? 0xC0000374ul : 0ul, 0x3000ul, 0, 0, 0);
    (void) efd_veh(NULL);
    CHECK(all_lines_labelled(),
          "not every emitted line carries FIRST-CHANCE and a trailing newline");
    for (i = 0; i < fk.n_said; i++) {
        CHECK(strstr(fk.said[i], "=== sightline CRASH ===") == NULL,
              "line %d imitates the product crash header", i);
        CHECK(strstr(fk.said[i], "ACCESS VIOLATION") == NULL,
              "line %d reuses the product's terminal-fault wording", i);
    }
    CHECK(said_count("not necessarily terminal") == fk.n_said,
          "only %d of %d lines say the event is not necessarily terminal",
          said_count("not necessarily terminal"), fk.n_said);
    /* the summary is the one line that is a summary rather than an event, and
     * it must still be unmistakable about what it is counting */
    fk.n_said = 0;
    (void) sl_early_fault_disarm(&HOST);
    CHECK(fk.n_said == 1, "the summary is %d line(s)", fk.n_said);
    CHECK(said_has("FIRST-CHANCE counts")
          && said_has("none of them is a claim about what ended the process"),
          "the summary does not disclaim what its counts mean: \"%s\"",
          fk.n_said > 0 ? fk.said[0] : "");
    printf("  label: every line says FIRST-CHANCE and not-necessarily-terminal,"
           " none imitates the product crash header, the summary disclaims its"
           " own counts\n");
    return g_fail == 0;
}

/* ======================== MODE: lifecycle ============================== */
/* Registration and removal, and the ORDER against the product handler. */
static int mode_lifecycle(void)
{
    fake_reset();
    CHECK(arm_ok() == 1, "arming failed");
    (void) fire_av(0x1000ul, 0, 4ul);
    (void) fire_av(0x2000ul, 0, 8ul);
    CHECK(sl_early_fault_disarm(&HOST) == 1, "the removal was not reported");
    normal_handler_add();
    CHECK(strcmp(order_str(), "add,remove,normal-add") == 0,
          "the sequence is \"%s\", expected \"add,remove,normal-add\" - the"
          " observer and the product handler must never coexist", order_str());
    CHECK(fk.removes == 1, "it removed %ld handler(s), expected 1", fk.removes);
    CHECK(s_armed == 0 && s_plat == NULL && s_handle == NULL,
          "a successful removal left module state behind");

    /* idempotent: a second disarm makes no platform call */
    fk.calls = 0;
    CHECK(sl_early_fault_disarm(&HOST) == 0, "a second disarm claimed a removal");
    CHECK(fk.calls == 0, "a second disarm made %ld platform call(s)", fk.calls);

    /* and a handler reached after removal is inert and still passes through */
    fk.n_said = 0;
    CHECK(fire_av(0x3000ul, 0, 4ul) == EXCEPTION_CONTINUE_SEARCH,
          "a late call did not pass through");
    CHECK(fk.n_said == 0, "a late call printed %d line(s)", fk.n_said);

    /* A REMOVAL THAT FAILS: reported, the observer is LEFT IN PLACE, and the
     * decision is said out loud. Leaving a pass-through observer registered
     * cannot change any exception's disposition; claiming it was removed when
     * it was not would make every later line a lie. */
    fake_reset();
    CHECK(arm_ok() == 1, "arming failed");
    fk.remove_fails = 1;
    fk.n_said = 0;
    CHECK(sl_early_fault_disarm(&HOST) == 0,
          "a failed removal was reported as a removal");
    CHECK(said_has("could NOT be removed"),
          "a failed removal was not reported: \"%s\"",
          fk.n_said > 0 ? fk.said[0] : "");
    CHECK(said_has("left in place") && said_has("only"),
          "the failed removal does not state the decision and its reason");
    CHECK(s_armed == 1 && s_plat != NULL,
          "a failed removal dropped the host the still-registered handler"
          " writes through - that would be a dangling sink");
    /* the still-registered observer keeps working and keeps passing through */
    fk.n_said = 0;
    CHECK(fire_av(0x4000ul, 0, 4ul) == EXCEPTION_CONTINUE_SEARCH,
          "the left-in-place observer changed the disposition");
    CHECK(fk.n_said == 1 && said_has("FIRST-CHANCE"),
          "the left-in-place observer stopped recording");
    CHECK(g_pager_calls == 0, "the pager was called");

    /* a host with no removal call: same decision, no crash */
    {
        struct sl_early_fault_host norm = HOST;
        fake_reset();
        norm.remove_veh = NULL;
        CHECK(arm_ok() == 1, "arming failed");
        CHECK(sl_early_fault_disarm(&norm) == 0,
              "a host with no removal call claimed a removal");
        CHECK(s_armed == 1, "it dropped state it could not undo");
    }
    printf("  lifecycle: add then remove then the product handler, idempotent,"
           " inert after removal, and a failed removal keeps a working"
           " pass-through observer rather than lying about it\n");
    return g_fail == 0;
}

/* ======================== MODE: default ================================= */
/* The path every launch takes that did not ask for this. */
static int mode_default(void)
{
    fake_reset();
    CHECK(sl_early_fault_arm(NULL, &HOST, 0x1111ul, 1) == 0,
          "the default launch armed");
    CHECK(sl_early_fault_disarm(&HOST) == 0, "the default launch disarmed");
    normal_handler_add();
    CHECK(fk.calls == 0,
          "the default launch made %ld platform call(s) across the whole"
          " lifecycle", fk.calls);
    CHECK(fk.n_said == 0, "the default launch printed %d line(s)", fk.n_said);
    CHECK(fk.adds == 0, "the default launch registered a vectored handler");
    CHECK(strcmp(order_str(), "normal-add") == 0,
          "the default launch's sequence is \"%s\", expected the product"
          " handler alone", order_str());
    CHECK(s_armed == 0 && s_plat == NULL && s_handle == NULL,
          "the default launch left module state set");
    CHECK(g_pager_calls == 0, "the default launch called the pager");

    /* a disarm reached with nothing armed - the ordering a future edit could
     * introduce - is a no-op rather than a stray removal */
    fake_reset();
    CHECK(sl_early_fault_disarm(&HOST) == 0, "a bare disarm claimed a removal");
    CHECK(fk.calls == 0, "a bare disarm made %ld call(s)", fk.calls);

    /* and a NULL host cannot be dereferenced on any entry point */
    fake_reset();
    CHECK(sl_early_fault_arm("1", NULL, 0ul, 0) == 0, "a NULL host armed");
    CHECK(sl_early_fault_arm("x", NULL, 0ul, 0) == 0,
          "a NULL host with a malformed value armed");
    CHECK(sl_early_fault_disarm(NULL) == 0, "a NULL host disarmed");
    CHECK(1, "a NULL host survived both entry points");

    /* THE PRODUCT'S OWN PLATFORM IS WIRED - inspected, never called. A suite
     * that only ever sees its own mocks would pass against a product whose
     * host was never filled in. */
    {
        const struct sl_early_fault_host *p = sl_early_fault_platform();
        CHECK(p != NULL, "the product platform is NULL");
        CHECK(p->add_veh != NULL && p->remove_veh != NULL
              && p->current_tid != NULL && p->region != NULL
              && p->emit != NULL,
              "the product platform has an unwired member");
        CHECK(p->emit == sl_early_fault_emit,
              "the product sink is not the emit function sl_main.c defines");
        CHECK(g_product_sink_calls == 0,
              "the PRODUCT host's sink was used %ld time(s) - a scenario"
              " reached the real platform instead of the fake one",
              g_product_sink_calls);
    }
    printf("  default: no registration, no line, no state, no pager; a NULL"
           " host is survivable; the product platform is wired (inspected,"
           " never called)\n");
    return g_fail == 0;
}

/* ======================== MODE: forged ================================= */
/* Hostile and partial inputs. Windows supplies both records in practice; the
 * product's own handler was once destroyed by a second fault inside the
 * instrument, so the instrument tests its own inputs. */
static int mode_forged(void)
{
    LONG r;

    fake_reset();
    CHECK(arm_ok() == 1, "arming failed");

    /* a NULL EXCEPTION_POINTERS */
    fk.n_said = 0;
    r = efd_veh(NULL);
    CHECK(r == EXCEPTION_CONTINUE_SEARCH,
          "a NULL pointer set returned %ld, not CONTINUE_SEARCH", (long) r);
    CHECK(fk.n_said == 1 && said_has("not readable"),
          "a NULL pointer set was not reported as unreadable");
    CHECK(all_lines_labelled(), "that line lost its label or its newline");

    /* a NULL ExceptionRecord inside a non-NULL pointer set */
    fk.n_said = 0;
    g_ep.ExceptionRecord = NULL;
    g_ep.ContextRecord = &g_ctx;
    r = efd_veh(&g_ep);
    CHECK(r == EXCEPTION_CONTINUE_SEARCH,
          "a NULL exception record returned %ld", (long) r);
    CHECK(fk.n_said == 1 && said_has("not readable"),
          "a NULL exception record was not reported");

    /* a NULL ContextRecord: the eip is unknown, everything else is recorded,
     * and nothing is dereferenced */
    fake_reset();
    CHECK(arm_ok() == 1, "arming failed");
    fk.n_said = 0;
    memset(&g_rec, 0, sizeof g_rec);
    g_rec.ExceptionCode = (DWORD) 0xC0000005ul;
    g_rec.NumberParameters = 2;
    g_rec.ExceptionInformation[0] = 0;
    g_rec.ExceptionInformation[1] = (ULONG_PTR) 0x4ul;
    g_ep.ExceptionRecord = &g_rec;
    g_ep.ContextRecord = NULL;
    r = efd_veh(&g_ep);
    CHECK(r == EXCEPTION_CONTINUE_SEARCH,
          "a NULL context record returned %ld", (long) r);
    CHECK(fk.n_said == 1, "it produced %d line(s)", fk.n_said);
    CHECK(said_has("0xc0000005") && said_has("0x00000004"),
          "a NULL context lost the code or the address: \"%s\"",
          fk.n_said > 0 ? fk.said[0] : "");
    CHECK(said_has("eip 0x00000000"),
          "an unknown eip was not printed as zero: \"%s\"",
          fk.n_said > 0 ? fk.said[0] : "");

    /* an absurd NumberParameters must not be taken as permission to read */
    fake_reset();
    CHECK(arm_ok() == 1, "arming failed");
    fk.n_said = 0;
    r = fire(0xC0000005ul, 0x1000ul, 0, 0x4ul, 0);
    CHECK(r == EXCEPTION_CONTINUE_SEARCH, "it returned %ld", (long) r);
    CHECK(said_has("no access/address information"),
          "a zero-parameter AV was read as if it carried an address");

    /* every one of these passed through, and none reached the pager */
    CHECK(g_pager_calls == 0, "the pager was called %ld time(s)", g_pager_calls);

    /* AND THE HANDLER NEVER RETURNS EXCEPTION_CONTINUE_EXECUTION, over every
     * shape driven in this mode. -1 is the value that would make this observer
     * a handler, and it must be unreachable. */
    fake_reset();
    CHECK(arm_ok() == 1, "arming failed");
    {
        unsigned long codes[6];
        int i;
        codes[0] = 0xC0000005ul; codes[1] = 0xE06D7363ul;
        codes[2] = 0x40010006ul; codes[3] = 0xC00000FDul;
        codes[4] = 0x00000000ul; codes[5] = 0xFFFFFFFFul;
        for (i = 0; i < 6; i++) {
            r = fire(codes[i], 0x1000ul, 0, 4ul, 2);
            CHECK(r != EXCEPTION_CONTINUE_EXECUTION,
                  "code 0x%08lx was HANDLED (returned CONTINUE_EXECUTION)",
                  codes[i]);
            CHECK(r == EXCEPTION_CONTINUE_SEARCH,
                  "code 0x%08lx returned %ld", codes[i], (long) r);
        }
    }
    printf("  forged: NULL pointer sets, a NULL context, and absurd parameter"
           " counts are survived and reported; nothing is ever handled\n");
    return g_fail == 0;
}

int main(int argc, char **argv)
{
    const char *mode = (argc > 1) ? argv[1] : "";
    int ok;

    fake_reset();
    if      (strcmp(mode, "parse")     == 0) ok = mode_parse();
    else if (strcmp(mode, "filter")    == 0) ok = mode_filter();
    else if (strcmp(mode, "arm")       == 0) ok = mode_arm();
    else if (strcmp(mode, "observe")   == 0) ok = mode_observe();
    else if (strcmp(mode, "cap")       == 0) ok = mode_cap();
    else if (strcmp(mode, "label")     == 0) ok = mode_label();
    else if (strcmp(mode, "lifecycle") == 0) ok = mode_lifecycle();
    else if (strcmp(mode, "default")   == 0) ok = mode_default();
    else if (strcmp(mode, "forged")    == 0) ok = mode_forged();
    else {
        printf("usage: %s parse|filter|arm|observe|cap|label|lifecycle"
               "|default|forged\n", argv[0]);
        return 2;
    }
    printf("  %s: %d check(s) passed, %d failed\n", mode, g_pass, g_fail);
    return ok ? 0 : 1;
}
