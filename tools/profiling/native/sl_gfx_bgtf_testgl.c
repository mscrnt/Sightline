/* SYNTHETIC GL for the background pass's GPU CLIP PREPASS. See the header for
 * how the shim gets in front of the real GL headers, what it fabricates and
 * what it refuses to provide. No window, no native graphics API, no game:
 * nothing here links against opengl32 or SDL.
 *
 * NOTE WHAT IS ABSENT, because the BUILD is what checks it. There is no
 * glMapBuffer, glUnmapBuffer, glGetBufferSubData or glReadPixels in
 * this file, no query entry point, no sync entry point AS A SYMBOL, and no
 * glDeleteBuffers - neither as a symbol nor in sl_gl_proc's table. The module
 * resolves everything outside its direct core names, so a read-back would be
 * either an undefined symbol at LINK time or a resolve that answers NULL and
 * makes the module refuse its own capability. Either way the fixture stops,
 * which is stronger than a counter: a counter only sees the paths a test
 * drives, whereas a missing symbol is checked over the whole translation unit.
 *
 * TWO DELETES EXIST HERE, AND NEITHER IS A SYMBOL. The neutral-program control
 * (SL_BG_TF_NEUTRAL_PROG) creates two objects it owns - one vertex shader and
 * one program - and releases each exactly once, so both deletes have to be
 * reachable. Each is reachable only through sl_gl_proc and only when a case sets
 * its own gate, `provide_delete_program` or `provide_delete_shader`, exactly as
 * glGetBufferSubData and glClientActiveTexture are, so: both are absent from the
 * twenty-four-name table and from the withhold-each-one battery, each default
 * resolve answers NULL - the shape of a context the control must refuse on, and
 * the gates are independent so either release alone can be withheld - and a
 * module calling either as a direct core name would still be an undefined symbol
 * at link time. The absence argument above is unchanged for glDeleteBuffers and
 * for every read-back, fence, sync and query entry point.
 *
 * AND BOTH DELETES GO THROUGH A PER-NAME LEDGER, not just a counter, because the
 * claim being checked is "each created object is deleted EXACTLY ONCE": matching
 * totals would also be produced by deleting one object twice and leaking
 * another. See the ledger note further down.
 *
 * glGetError IS ONE OF THE DIRECT CORE NAMES and so IS a symbol here. It is a
 * GL 1.0 core export, it is not a read-back or a synchronisation point, and
 * resolving it through the extension loader is the defect the direct call
 * replaces. "An off launch consumes no GL error" is therefore carried by
 * `get_error_calls` rather than by the absence of the symbol.
 *
 * glFinish IS NOW A DIRECT CORE NAME TOO, AND THE ABSENCE LIST IS ONE NAME
 * SHORTER THAN IT WAS. The post-reset boundary control (SL_BG_TF_POST_RESET)
 * issues one glFinish on two of its arms, and glFinish is a GL 1.0 core export
 * that wglGetProcAddress is documented to answer NULL for, so the module calls
 * it directly exactly as it calls glEnable, glDrawArrays and glGetError. That
 * means the link-time absence argument no longer covers glFinish: it is a real
 * symbol here, and "an off launch adds no synchronisation point" is carried by
 * the counter `finish_calls` and by the ordered transcript, which the off cases
 * assert, rather than by the build refusing to link. The argument is unchanged
 * for every OTHER name on the list above - no read-back, fence, sync or query
 * entry point exists here as a symbol - and glFinish is not a read-back: it
 * returns nothing and the module reads nothing from it.
 *
 * glFlush IS A DIRECT CORE NAME NOW AS WELL, ON EXACTLY THOSE TERMS, and the
 * absence list is one further name shorter. The fence synchronisation mode
 * (SL_BG_TF_AUX_SYNC=FENCE) flushes the PRODUCER'S context at each of its two
 * cross-context handoffs, because OpenGL 4.5 core section 4.1.2 is explicit
 * that a consumer's flush bit does not reach the producer's command stream, and
 * glFlush is a GL 1.0 core export the extension loader answers NULL for. So it
 * is a real symbol here and "a FINISH launch and an off launch add no flush" is
 * carried by `flush_calls`, split per context, and by the ordered transcript.
 *
 * THE THREE SYNC ENTRY POINTS ARE NOT SYMBOLS, and that distinction is the whole
 * of what the absence argument still buys here. glFenceSync, glWaitSync and
 * glDeleteSync are GL 3.2, they are reachable only through sl_gl_proc, and only
 * when a case sets `provide_fence_set` - exactly as the two deletes and the
 * readback are - so a module calling one as a direct core name would still be an
 * undefined symbol at link time over the whole translation unit, and a FINISH
 * launch that resolved one would be visible as a resolve this shim answers NULL
 * for. They are also PER-CONTEXT PAIRS, so an address resolved on one context
 * and invoked on the other raises v_ptr_wrong_context rather than passing.
 *
 * THE STATE MODEL IS GL'S, NOT A CONVENIENCE. glUniform writes the program in
 * use, so with none current it raises INVALID_OPERATION and writes nothing, and
 * a location of -1 is silently ignored; a client pointer command captures the
 * array buffer bound AT THE TIME IT RUNS, which is what makes its pointer
 * argument an offset rather than an address. Both are modelled rather than
 * relaxed, because the module's claims are about exactly those two rules.
 */
#include "sl_gfx_bgtf_testgl.h"

#include <string.h>

/* ---- HOST I/O FAILURES, INJECTED AT THE LINK ----------------------------
 *
 * Compiled only under BGTF_WRAP_IO, and reached only in the builds that pass
 * -Wl,--wrap=malloc,--wrap=fwrite,--wrap=fflush,--wrap=fclose. See the long
 * note at `io_fault` in the header for why the fault is injected here rather
 * than by a hook in src/: the product source is byte-identical in these builds,
 * and the four outcomes the diagnostic keeps apart - an allocation that fails,
 * a write that is short, a flush that fails, a close that fails - cannot be
 * produced on demand any other way.
 *
 * The master switch means the fixture's own allocations and its own stdio are
 * never the ones that fail; a case turns it on for the capture alone. */
#ifdef BGTF_WRAP_IO
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

void  *__real_malloc(size_t n);
size_t __real_fwrite(const void *p, size_t a, size_t b, FILE *f);
int    __real_fflush(FILE *f);
int    __real_fclose(FILE *f);

/* 1 when THIS call is the one to break. -1 in the counter means never, and the
 * injection clears the master switch so it fires exactly once: the code that
 * handles a failure reports it through stdio, which allocates, so a fault left
 * armed would break the error path instead of testing it. */
static int io_bite(long *after)
{
    if (!tf.io_fault || *after < 0L) return 0;
    if (*after > 0L) { (*after)--; return 0; }
    tf.io_injected++;
    tf.io_fault = 0;
    return 1;
}

void *__wrap_malloc(size_t n)
{
    if (tf.io_fault) tf.io_malloc++;
    if (io_bite(&tf.malloc_fail_after)) return NULL;
    return __real_malloc(n);
}

size_t __wrap_fwrite(const void *p, size_t a, size_t b, FILE *f)
{
    if (tf.io_fault) tf.io_fwrite++;
    if (io_bite(&tf.fwrite_short_after)) {
        /* THE SHORT PREFIX IS REALLY WRITTEN, because "partial bytes are
         * preserved" is one of the claims and there would be nothing to
         * preserve if this wrote nothing. One element short of the request. */
        const size_t total = a * b;
        const size_t keep = total > a ? total - a : 0u;
        if (keep != 0u) (void) __real_fwrite(p, 1u, keep, f);
        return b > 0u ? b - 1u : 0u;
    }
    return __real_fwrite(p, a, b, f);
}

int __wrap_fflush(FILE *f)
{
    if (tf.io_fault) tf.io_fflush++;
    if (io_bite(&tf.fflush_fail_after)) { (void) __real_fflush(f); return -1; }
    return __real_fflush(f);
}

/* A COMPETING WRITER TAKES THE PATH DURING THE CAPTURE.
 *
 * WHAT IT MODELS AND WHAT IT CANNOT. Two writers racing for the same name. The
 * narrowest form of that race - a file appearing between a writer's "is this
 * path free?" and its subsequent truncating open - is what destroyed records
 * under the old probe-then-write design, and it CANNOT be reproduced against
 * the current one because that window does not exist: the create is a single
 * O_CREAT|O_EXCL operation, and there is no call inside it to interpose on. The
 * primitive itself is not interposable here either - mingw declares it
 * __declspec(dllimport), so the reference is an import stub and --wrap has no
 * symbol to rewrite.
 *
 * So this places the file in the widest window that still exists: the capture
 * has begun and has already written one payload, and the path for the NEXT one
 * becomes occupied an instant before its own create runs. `race_path` is
 * created with `race_bytes` when the Nth close goes through, which is after the
 * previous payload is on disk and before the next create. The create must then
 * refuse, and the bytes that appeared must be untouched.
 *
 * SINGLE-SHOT, like the other injections: it clears the master switch on the way
 * out, so the sidecar's own write afterwards is an ordinary one.
 *
 * The real fwrite, fflush and fclose are called rather than the wrapped ones, so
 * the racing creator's own stdio is never what a counter is measuring. */
static void race_bite(void)
{
    FILE *g;
    if (!tf.io_fault || tf.race_on_fclose_after < 0L) return;
    if (tf.race_on_fclose_after > 0L) { tf.race_on_fclose_after--; return; }
    g = fopen(tf.race_path, "wb");
    if (g != NULL) {
        (void) __real_fwrite(tf.race_bytes, 1u, strlen(tf.race_bytes), g);
        (void) __real_fflush(g);
        (void) __real_fclose(g);
        tf.race_fired++;
    }
    tf.io_injected++;
    tf.io_fault = 0;
}

int __wrap_fclose(FILE *f)
{
    if (tf.io_fault) tf.io_fclose++;
    race_bite();
    if (io_bite(&tf.fclose_fail_after)) {
        /* The handle is really closed - leaking it would make a later case fail
         * for an unrelated reason - and the failure is in the ANSWER, which is
         * what a caller checking fclose's return sees. */
        (void) __real_fclose(f);
        return -1;
    }
    return __real_fclose(f);
}
#endif /* BGTF_WRAP_IO */

/* ---- THE FIXTURE'S OWN FAULT INJECTION ----------------------------------
 *
 * A suite that passes on the first build has proved nothing until it is shown
 * able to fail. Each BGTF_MUT_* below breaks ONE thing the fake driver
 * observes, and tools/windows/bgtftest.ps1 builds a separate executable for
 * each and requires it to FAIL. They are fault injection into the SYNTHETIC
 * DRIVER, never into the module: src/gfx/sl_gfx_bgtf.c is compiled from the
 * same unmodified source in every one of these builds, and a mutant that
 * passed would mean the corresponding assertion does not bite.
 *
 *   DRAWRANGE   the prepass draw is reported as starting at vertex 1, which is
 *               the canonical-order property (output i is input i) made false
 *   VPSIZE      the position array is reported as three floats, which is a
 *               model position where a clip position is required
 *   ARRBUF      a bind of 0 to GL_ARRAY_BUFFER does nothing, so a buffer is
 *               left bound and every later client pointer would be an offset
 *   LOCSHIFT    every uniform resolves one location away from the one the test
 *               reads, so the transform's uploads land somewhere else
 *   NODISCARD   enabling rasterizer discard does nothing, so the prepass draw
 *               could reach a fragment
 *   ALLOCOK     the GL_BUFFER_SIZE query always reports the size that was
 *               REQUESTED rather than the size that was installed - a driver
 *               whose metadata lies, which is the only thing standing between
 *               a short allocation and a pool stamped valid and drawn from.
 *               It breaks every assertion that an undersized, empty or
 *               unverifiable allocation is refused.
 *   SONODRAW     glDrawArrays raises neither of the draw counters, so "the
 *                state-only control issues ZERO prepass draws" would be true of
 *                a driver that simply never counts one. It is run in `so ref`
 *                mode, where the REFERENCE arm requires exactly one counted draw
 *                over nverts vertices - which is what makes the other arm's zero
 *                a reading of a working counter rather than an arrangement.
 *   SONOTFBIND   glBindBufferBase does not raise its counter, so the
 *                transform-feedback binding the state-only control is required to
 *                KEEP in its setup becomes unobservable. It is run in `so only`
 *                mode and breaks the assertion that the indexed binding was made
 *                and released inside the bracket whose draw was omitted.
 *
 * THE POSITION-ONLY-INPUT CONTROL'S THREE, each the shape of one way an exact
 * restoration can go wrong. All run in that control's own mode.
 *
 *   PI_NORESTORE one disabled array is never put back: the enable that would
 *                restore the colour array is swallowed, so the state after the
 *                draw differs from the state before it in exactly one field.
 *                Breaks the field-for-field comparison and the arrays_back
 *                count, which is what "restore EXACTLY" has to mean.
 *   PI_BLINDENABLE  the restore enables the whole set this control knows about
 *                rather than the set the save found enabled. An array that was
 *                OFF comes back ON, which hands the following raster an array
 *                the pass never asked for - the defect that makes "never
 *                blind-enable" a rule rather than a preference.
 *   PI_WRONGUNIT the per-unit selector lands one unit away, so every per-unit
 *                read and write addresses the wrong array and a save of unit 1
 *                restores unit 2. Breaks the multi-unit comparison, which a
 *                single-unit fixture could not have caught at all.
 *
 * AND THREE FOR THE BOUNDED DOMAIN AND THE VALIDATED SELECTOR.
 *
 *   PI_UNITSDOMAIN  the GL_MAX_TEXTURE_COORDS query answers the CONVENTIONAL
 *                unit count instead of the coordinate-set count - the domain a
 *                sweep bounded by GL_MAX_TEXTURE_UNITS would have covered. Run
 *                in `pi coords` mode, where a coordinate array is enabled on a
 *                set ABOVE the unit count: that array then survives into the
 *                real prepass draw, which is the exposure the wider domain
 *                exists to remove.
 *   PI_SELSANE   an illegal or out-of-domain answer to
 *                GL_CLIENT_ACTIVE_TEXTURE is repaired into the modelled
 *                selector before the module sees it, so a module that never
 *                validated the saved selector would be indistinguishable from
 *                one that did. Run in `pi selbad` mode, whose refusal, zero
 *                work counts and untouched context all become false.
 *   PI_SAVEPLAUSIBLE  a GL_CLIENT_ACTIVE_TEXTURE query that writes NOTHING
 *                instead writes the modelled selector, which is a plausible
 *                value. Run in `pi selnone` mode. The module can only tell a
 *                non-answer from an answer because the save is seeded with a
 *                value no answer can be; a save left at 0 is the same defect,
 *                since 0 would read as an answer the context never gave.
 *
 * None is defined in an ordinary build, so the shipped fixture carries no
 * injected fault.
 */

struct sl_bgtfgl tf;

/* The uniforms the module resolves, in the order sl_bgtf_ready asks for them.
 * The locations are this stub's invention: 100 + the index, stable across
 * generations so a test can name a location by its uniform. */
const char *const sl_bgtfgl_uniform_names[BGTFGL_UNIFORMS] = {
    "u_sc", "u_post", "u_org", "u_C", "u_fogless", "u_dmode", "u_fk1", "u_fk2"
};

/* The entry points the module resolves, in the order sl_bgtf_ready asks for
 * them: the sixteen GL 2.0 program calls, then the four transform-feedback
 * calls, then the four buffer-object calls - the last of which is
 * glGetBufferParameteriv, the allocation-acceptance query. Nothing else is
 * here, so a module that reached for a sync point, a read-back or a delete
 * would get NULL. */
const char *const sl_bgtfgl_proc_names[BGTFGL_PROCS] = {
    "glCreateShader", "glShaderSource", "glCompileShader", "glGetShaderiv",
    "glGetShaderInfoLog", "glCreateProgram", "glAttachShader", "glLinkProgram",
    "glGetProgramiv", "glGetProgramInfoLog", "glUseProgram",
    "glGetUniformLocation", "glUniform1f", "glUniform3f", "glUniform1i",
    "glUniform1fv",
    "glTransformFeedbackVaryings", "glBindBufferBase",
    "glBeginTransformFeedback", "glEndTransformFeedback",
    "glGenBuffers", "glBindBuffer", "glBufferData", "glGetBufferParameteriv"
};

#define BGTFGL_LOC_BASE 100

/* A name carries the generation that minted it, so a name used after
 * sl_bgtfgl_new_context is detectable rather than merely suspicious. The three
 * kinds are kept in separate sequence ranges so a program name can never be
 * mistaken for a buffer name. */
#define BGTFGL_GEN(n)    ((int) ((n) >> 12))
#define BGTFGL_MINT(seq) ((GLuint) (((GLuint) tf.context << 12) | (GLuint) (seq)))

static int shader_seq, program_seq, buffer_seq;

/* The buffer registry: a name, the byte size the last glBufferData gave it, and
 * the bytes themselves.
 *
 * Sizes are what makes the overrun watchdog possible - a prepass draw of n
 * vertices writes n * 4 floats, and a pool sized for fewer would be a write
 * past the end. CONTENTS are what makes the diagnostic's comparison contract
 * checkable: the record it writes is bytes, so a test has to be able to install
 * a known pattern and find it unchanged in the file. The store is static and
 * capped per buffer, so an absurd vertex count cannot make the fixture
 * allocate - it simply has no store and a read of it writes nothing. */
static struct {
    GLuint        name;
    long          bytes;
    long          store;                  /* bytes actually backed below */
    unsigned char data[BGTFGL_POOL_CAP];
} bufs[BGTFGL_BUFFERS];
static int nbufs;

/* THE SHADER AND PROGRAM LEDGERS, which exist for one claim: each object this
 * shim handed out was deleted EXACTLY ONCE, and nothing was deleted that was
 * never handed out.
 *
 * WHY NAMES AND NOT COUNTS. "created == deleted" is satisfied by a module that
 * deletes one object twice and leaks another, so the check has to be per NAME.
 * A create appends the name; a delete looks it up and REMOVES it, so a second
 * delete of the same name finds nothing and is recorded as a double delete
 * rather than merely as a surplus call. Whatever is left at the end of a run is
 * a leak, and `live_shaders` / `live_programs` are those lengths.
 *
 * They are file-static, not fields, because a measurement window must not reset
 * them: a case zeroes the counters mid-run and the lifetime claim spans the
 * whole context. A new fake context empties them, the names of a dead context
 * being gone with it. */
#define BGTFGL_OBJS 32
static GLuint live_sh[BGTFGL_OBJS];
static int    nlive_sh;
static GLuint live_pr[BGTFGL_OBJS];
static int    nlive_pr;

static void obj_born(GLuint *list, int *n, GLuint name)
{
    if (name == 0u) return;
    if (*n < BGTFGL_OBJS) list[(*n)++] = name;
}

/* 1 when the name was live and is now removed, 0 when it was not live at all -
 * which is either a double delete or a name this shim never minted, and the
 * caller tells those apart from the sequence it minted. */
static int obj_died(GLuint *list, int *n, GLuint name)
{
    int i;
    for (i = 0; i < *n; i++) {
        if (list[i] != name) continue;
        list[i] = list[*n - 1];
        (*n)--;
        return 1;
    }
    return 0;
}

static void obj_publish(void)
{
    tf.live_shaders = (long) nlive_sh;
    tf.live_programs = (long) nlive_pr;
}

void sl_bgtfgl_reset(void)
{
    memset(&tf, 0, sizeof tf);
    tf.context = 1;
    /* THE ALLOCATION CONTROLS START NEUTRAL, and neutral is not zero for
     * `getbufparam_bytes`: 0 would be a query reporting an empty buffer, which
     * is a failure injection rather than a default. -1 means "report what was
     * installed". */
    tf.getbufparam_bytes = -1;
    /* GL'S OWN DEFAULT CLIENT ACTIVE UNIT is GL_TEXTURE0, and 0 is not it: the
     * token is 0x84C0, so a zeroed struct would describe a unit index of
     * -0x84C0 and every per-unit read would be out of range. Every client array
     * starts disabled, which is GL's default too; a case that needs inherited
     * arrays installs them with sl_bgtfgl_test_client. */
    tf.client_unit = GL_TEXTURE0;
    /* AND NEUTRAL FOR THE READBACK IS -1 TOO: 0 would be a read that writes
     * nothing, which is an injected failure rather than a default. */
    tf.getsub_bytes = -1;
    /* AND NEVER, for the four host-I/O faults: a count of 0 would mean "fail
     * the very first", which no ordinary fixture wants. */
    tf.malloc_fail_after = -1;
    tf.fwrite_short_after = -1;
    tf.fflush_fail_after = -1;
    tf.fclose_fail_after = -1;
    /* AND NEVER, for the concurrent-create injection: 0 would mean "race the
     * very first close", which no ordinary fixture wants either. */
    tf.race_on_fclose_after = -1;
    /* AND NEVER FOR THE THREE CONTEXT-FAILURE GATES, for the same reason: 0
     * means "fail the very first call", which is a failure injection rather
     * than a default. The current context starts at 0, the MAIN one, which is
     * what a backend hands a module - so a zeroed struct is already the right
     * answer there and no assignment is needed for it. A note rather than a
     * line, because a reader looking for where `ctx_cur` is initialised has to
     * find something. */
    tf.aux_create_fail_after = -1;
    tf.aux_switch_fail_after = -1;
    tf.main_switch_fail_after = -1;
    /* AND FOR THE UNKNOWN-OUTCOME GATE, on the same terms. */
    tf.aux_switch_unknown_after = -1;
    /* THE REAL-PREPASS MODE'S OWN GATE, -1 for "never", on the same line of
     * reasoning every gate above is initialised on. */
    tf.is_buffer_false_after = -1;
    tf.is_program_false_after = -1;
    /* AND THE FENCE PATH'S TWO CREATION GATES, -1 for "never", on the same line
     * of reasoning every gate above is initialised on (SL_BG_TF_AUX_SYNC). */
    tf.fence_zero_main_after = -1;
    tf.fence_zero_aux_after = -1;
    shader_seq = 0;
    program_seq = 0;
    buffer_seq = 0;
    memset(bufs, 0, sizeof bufs);
    nbufs = 0;
    nlive_sh = 0;
    nlive_pr = 0;
    obj_publish();
}

void sl_bgtfgl_log_reset(void)
{
    struct sl_bgtfgl keep = tf;
    memset(&tf, 0, sizeof tf);
    tf.withhold_proc       = keep.withhold_proc;
    tf.withhold_uniform    = keep.withhold_uniform;
    tf.create_shader_zero  = keep.create_shader_zero;
    tf.create_program_zero = keep.create_program_zero;
    tf.fail_compile        = keep.fail_compile;
    tf.fail_link           = keep.fail_link;
    tf.gen_buffers_zero    = keep.gen_buffers_zero;
    tf.alloc_short_initial = keep.alloc_short_initial;
    tf.alloc_short_growth  = keep.alloc_short_growth;
    tf.alloc_bytes         = keep.alloc_bytes;
    tf.alloc_install_none  = keep.alloc_install_none;
    tf.getbufparam_fail    = keep.getbufparam_fail;
    tf.getbufparam_bytes   = keep.getbufparam_bytes;
    tf.provide_delete_program = keep.provide_delete_program;
    tf.provide_delete_shader  = keep.provide_delete_shader;
    tf.provide_readback    = keep.provide_readback;
    memcpy(tf.geterror_queue, keep.geterror_queue, sizeof tf.geterror_queue);
    tf.ngeterror_queue     = keep.ngeterror_queue;
    /* THE SYNTHETIC ERROR INJECTION IS THE TEST'S CHOICE ABOUT THE FAKE
     * DRIVER, exactly as the allocation and readback controls above are: a case
     * arms it, zeroes the counters, and drives the bracket. The OCCURRENCE
     * CURSOR is kept with it - a case that armed "the second draw" and then
     * opened a measurement window must still get the second draw - and so is
     * the FLAGGED STATE, which is modelled GL state like tf.current and not an
     * observation: a flag a driver has set is still set when a test stops
     * counting. The per-context READ COUNTS are observations and are not kept. */
    tf.inject_error_on     = keep.inject_error_on;
    tf.inject_error_after  = keep.inject_error_after;
    tf.inject_error_code   = keep.inject_error_code;
    tf.inject_error_count  = keep.inject_error_count;
    tf.inject_error_seen   = keep.inject_error_seen;
    tf.inject_error_stuck  = keep.inject_error_stuck;
    memcpy(tf.err_pending, keep.err_pending, sizeof tf.err_pending);
    memcpy(tf.nerr_pending, keep.nerr_pending, sizeof tf.nerr_pending);
    tf.getsub_fail         = keep.getsub_fail;
    tf.getsub_bytes        = keep.getsub_bytes;
    tf.getsub_zero         = keep.getsub_zero;
    tf.io_fault            = keep.io_fault;
    tf.malloc_fail_after   = keep.malloc_fail_after;
    tf.fwrite_short_after  = keep.fwrite_short_after;
    tf.fflush_fail_after   = keep.fflush_fail_after;
    tf.fclose_fail_after   = keep.fclose_fail_after;
    tf.race_on_fclose_after = keep.race_on_fclose_after;
    memcpy(tf.race_bytes, keep.race_bytes, sizeof tf.race_bytes);
    memcpy(tf.race_path,  keep.race_path,  sizeof tf.race_path);
    tf.context             = keep.context;
    tf.context_destroyed   = keep.context_destroyed;
    /* THE AUXILIARY CONTEXT IS NOT A MEASUREMENT WINDOW EITHER, for the reason
     * the object ledgers below are not: a case zeroes the counters to measure
     * one bracket, and the context alive at that moment is still alive and is
     * still the module's to destroy. The CURRENT context survives too, because
     * it is modelled state like `tf.current` and not an observation - wiping it
     * would hand the next call a main context nobody switched back to and make
     * every attribution read as correct. The three failure gates and their
     * counts survive for the reason every other injection does: a case arms one,
     * zeroes the counters, and drives the bracket. */
    tf.aux_exists          = keep.aux_exists;
    tf.ctx_cur             = keep.ctx_cur;
    tf.aux_create_fail     = keep.aux_create_fail;
    tf.aux_create_lost     = keep.aux_create_lost;
    tf.aux_switch_fail     = keep.aux_switch_fail;
    tf.main_switch_fail    = keep.main_switch_fail;
    tf.aux_create_fail_after  = keep.aux_create_fail_after;
    tf.aux_switch_fail_after  = keep.aux_switch_fail_after;
    tf.main_switch_fail_after = keep.main_switch_fail_after;
    tf.aux_switch_unknown     = keep.aux_switch_unknown;
    tf.aux_switch_unknown_after = keep.aux_switch_unknown_after;
    tf.aux_destroy_unknown    = keep.aux_destroy_unknown;
    tf.withhold_proc_on_aux   = keep.withhold_proc_on_aux;
    /* THE TERMINATE IS A FACT ABOUT THE PROCESS rather than about a
     * measurement window: the product would not exist after it, so a count
     * zeroed across it would let a case assert "nothing was called after the
     * loss" in a window that began after the loss. Both it and the
     * after-count survive, here and across a new main context. */
    tf.fatal_calls         = keep.fatal_calls;
    tf.fatal_code          = keep.fatal_code;
    tf.calls_after_fatal   = keep.calls_after_fatal;
    tf.provide_is_program  = keep.provide_is_program;
    tf.is_program_false    = keep.is_program_false;
    /* AND THE REAL-PREPASS MODE'S CAPABILITY GATES, kept for the reason every
     * other gate is kept across a measurement window or a context recreation:
     * they are the TEST'S choices about the fake driver, not observations of the
     * module. The bound array object is NOT kept across a context recreation -
     * it is modelled state and a new context has new state - and is reset below
     * with the rest. */
    tf.provide_real_set      = keep.provide_real_set;
    tf.is_buffer_false       = keep.is_buffer_false;
    tf.is_program_false_after = keep.is_program_false_after;
    tf.is_buffer_false_after = keep.is_buffer_false_after;
    tf.gen_vao_zero          = keep.gen_vao_zero;
    /* AND THE FENCE PATH'S GATES AND ITS LIVE HANDLES (SL_BG_TF_AUX_SYNC). The
     * gates are the TEST'S choices about the fake driver, like every gate above.
     * The TABLE is modelled state and not an observation: a sync object alive
     * when a case zeroes the counters is still alive and is still the module's to
     * delete, so wiping it would make every use-after-free and every leak check
     * vacuous. The per-context CALL COUNTS and the leak totals are observations
     * and are deliberately not kept. */
    tf.provide_fence_set     = keep.provide_fence_set;
    tf.withhold_fence_main   = keep.withhold_fence_main;
    tf.withhold_fence_aux    = keep.withhold_fence_aux;
    tf.fence_zero_main       = keep.fence_zero_main;
    tf.fence_zero_aux        = keep.fence_zero_aux;
    tf.fence_zero_main_after = keep.fence_zero_main_after;
    tf.fence_zero_aux_after  = keep.fence_zero_aux_after;
    memcpy(tf.sync_tab,      keep.sync_tab,      sizeof tf.sync_tab);
    memcpy(tf.sync_tab_ctx,  keep.sync_tab_ctx,  sizeof tf.sync_tab_ctx);
    memcpy(tf.sync_tab_live, keep.sync_tab_live, sizeof tf.sync_tab_live);
    tf.nsync_tab             = keep.nsync_tab;
    tf.current             = keep.current;
    tf.arr_buf             = keep.arr_buf;
    tf.elem_buf            = keep.elem_buf;
    tf.tf_buf              = keep.tf_buf;
    tf.discard             = keep.discard;
    tf.tf_active           = keep.tf_active;
    tf.last_shader         = keep.last_shader;
    tf.last_program        = keep.last_program;
    tf.vptr                = keep.vptr;
    /* THE CLIENT-ARRAY STATE IS MODELLED STATE, so a measurement window keeps
     * it exactly as the counters' window keeps the bindings: a case dirties the
     * arrays, zeroes the counters, drives a draw, and compares. Wiping them here
     * would make every restore read as correct against GL's defaults. */
    tf.client_unit         = keep.client_unit;
    memcpy(tf.cli_t, keep.cli_t, sizeof tf.cli_t);
    tf.cli_v = keep.cli_v; tf.cli_c = keep.cli_c; tf.cli_n = keep.cli_n;
    tf.cli_i = keep.cli_i; tf.cli_e = keep.cli_e; tf.cli_s = keep.cli_s;
    tf.cli_f = keep.cli_f;
    tf.provide_client_active_texture = keep.provide_client_active_texture;
    tf.units_reported      = keep.units_reported;
    tf.coords_reported     = keep.coords_reported;
    tf.getintegerv_fail    = keep.getintegerv_fail;
    tf.client_unit_fail    = keep.client_unit_fail;
    tf.client_unit_forced  = keep.client_unit_forced;
    tf.client_unit_answer  = keep.client_unit_answer;
    /* THE OBJECT LEDGERS ARE NOT A MEASUREMENT WINDOW. A case zeroes the
     * counters to measure one bracket, but the objects alive at that moment are
     * still alive, so the published lengths are restored from the ledgers rather
     * than left at the memset's zeros - which would read as "nothing is alive"
     * and make every leak check vacuous. */
    obj_publish();
}

void sl_bgtfgl_new_context(void)
{
    const struct sl_bgtfgl keep = tf;
    memset(&tf, 0, sizeof tf);
    tf.withhold_proc       = keep.withhold_proc;
    tf.withhold_uniform    = keep.withhold_uniform;
    tf.create_shader_zero  = keep.create_shader_zero;
    tf.create_program_zero = keep.create_program_zero;
    tf.fail_compile        = keep.fail_compile;
    tf.fail_link           = keep.fail_link;
    tf.gen_buffers_zero    = keep.gen_buffers_zero;
    tf.alloc_short_initial = keep.alloc_short_initial;
    tf.alloc_short_growth  = keep.alloc_short_growth;
    tf.alloc_bytes         = keep.alloc_bytes;
    tf.alloc_install_none  = keep.alloc_install_none;
    tf.getbufparam_fail    = keep.getbufparam_fail;
    tf.getbufparam_bytes   = keep.getbufparam_bytes;
    tf.provide_delete_program = keep.provide_delete_program;
    tf.provide_delete_shader  = keep.provide_delete_shader;
    tf.provide_readback    = keep.provide_readback;
    memcpy(tf.geterror_queue, keep.geterror_queue, sizeof tf.geterror_queue);
    tf.ngeterror_queue     = keep.ngeterror_queue;
    /* THE SYNTHETIC ERROR INJECTION IS THE TEST'S CHOICE ABOUT THE FAKE
     * DRIVER, exactly as the allocation and readback controls above are: a case
     * arms it, zeroes the counters, and drives the bracket. The OCCURRENCE
     * CURSOR is kept with it - a case that armed "the second draw" and then
     * opened a measurement window must still get the second draw - and so is
     * the FLAGGED STATE, which is modelled GL state like tf.current and not an
     * observation: a flag a driver has set is still set when a test stops
     * counting. The per-context READ COUNTS are observations and are not kept. */
    tf.inject_error_on     = keep.inject_error_on;
    tf.inject_error_after  = keep.inject_error_after;
    tf.inject_error_code   = keep.inject_error_code;
    tf.inject_error_count  = keep.inject_error_count;
    tf.inject_error_seen   = keep.inject_error_seen;
    tf.inject_error_stuck  = keep.inject_error_stuck;
    memcpy(tf.err_pending, keep.err_pending, sizeof tf.err_pending);
    memcpy(tf.nerr_pending, keep.nerr_pending, sizeof tf.nerr_pending);
    tf.getsub_fail         = keep.getsub_fail;
    tf.getsub_bytes        = keep.getsub_bytes;
    tf.getsub_zero         = keep.getsub_zero;
    tf.io_fault            = keep.io_fault;
    tf.malloc_fail_after   = keep.malloc_fail_after;
    tf.fwrite_short_after  = keep.fwrite_short_after;
    tf.fflush_fail_after   = keep.fflush_fail_after;
    tf.fclose_fail_after   = keep.fclose_fail_after;
    tf.race_on_fclose_after = keep.race_on_fclose_after;
    memcpy(tf.race_bytes, keep.race_bytes, sizeof tf.race_bytes);
    memcpy(tf.race_path,  keep.race_path,  sizeof tf.race_path);
    tf.context             = keep.context + 1;
    /* THE AUXILIARY CONTEXT SURVIVES A NEW MAIN CONTEXT, which is the one place
     * this shim models something that is NOT per main context. It exists because
     * the module deliberately keeps its own record of it across
     * sl_bgtf_invalidate: that hook calls no platform function by contract, so
     * dropping the record there would be the module forgetting a context the
     * platform still holds. Clearing it here would make that leak unobservable.
     * The CURRENT context survives for the same reason - a new main context
     * does not switch anything by itself - and the three failure gates survive
     * because they are a property of the TEST, exactly as the allocation and
     * readback controls above are.
     *
     * A NEW CONTEXT HAS NEW STATE, so the client arrays go back to GL's own
     * defaults - everything disabled, unit 0 selected - rather than carrying the
     * dead context's. */
    tf.aux_exists          = keep.aux_exists;
    tf.ctx_cur             = keep.ctx_cur;
    tf.aux_create_fail     = keep.aux_create_fail;
    tf.aux_create_lost     = keep.aux_create_lost;
    tf.aux_switch_fail     = keep.aux_switch_fail;
    tf.main_switch_fail    = keep.main_switch_fail;
    tf.aux_create_fail_after  = keep.aux_create_fail_after;
    tf.aux_switch_fail_after  = keep.aux_switch_fail_after;
    tf.main_switch_fail_after = keep.main_switch_fail_after;
    tf.aux_switch_unknown     = keep.aux_switch_unknown;
    tf.aux_switch_unknown_after = keep.aux_switch_unknown_after;
    tf.aux_destroy_unknown    = keep.aux_destroy_unknown;
    tf.withhold_proc_on_aux   = keep.withhold_proc_on_aux;
    /* THE TERMINATE IS A FACT ABOUT THE PROCESS rather than about a
     * measurement window: the product would not exist after it, so a count
     * zeroed across it would let a case assert "nothing was called after the
     * loss" in a window that began after the loss. Both it and the
     * after-count survive, here and across a new main context. */
    tf.fatal_calls         = keep.fatal_calls;
    tf.fatal_code          = keep.fatal_code;
    tf.calls_after_fatal   = keep.calls_after_fatal;
    tf.provide_is_program  = keep.provide_is_program;
    tf.is_program_false    = keep.is_program_false;
    /* AND THE REAL-PREPASS MODE'S CAPABILITY GATES, kept for the reason every
     * other gate is kept across a measurement window or a context recreation:
     * they are the TEST'S choices about the fake driver, not observations of the
     * module. The bound array object is NOT kept across a context recreation -
     * it is modelled state and a new context has new state - and is reset below
     * with the rest. */
    tf.provide_real_set      = keep.provide_real_set;
    tf.is_buffer_false       = keep.is_buffer_false;
    tf.is_program_false_after = keep.is_program_false_after;
    tf.is_buffer_false_after = keep.is_buffer_false_after;
    tf.gen_vao_zero          = keep.gen_vao_zero;
    /* AND THE FENCE PATH'S GATES, kept for the reason every other gate is kept
     * across a context recreation. THE LIVE HANDLES ARE NOT: a sync object
     * belongs to the share group of the context that made it, so the old
     * context's objects are gone with it and a handle from one is a stale handle,
     * which v_sync_unknown already records. The leak claim is therefore per
     * context, which is the scope the module's own acquire and teardown work
     * in - the same scope the object ledgers below use. */
    tf.provide_fence_set     = keep.provide_fence_set;
    tf.withhold_fence_main   = keep.withhold_fence_main;
    tf.withhold_fence_aux    = keep.withhold_fence_aux;
    tf.fence_zero_main       = keep.fence_zero_main;
    tf.fence_zero_aux        = keep.fence_zero_aux;
    tf.fence_zero_main_after = keep.fence_zero_main_after;
    tf.fence_zero_aux_after  = keep.fence_zero_aux_after;
    tf.client_unit         = GL_TEXTURE0;
    tf.provide_client_active_texture = keep.provide_client_active_texture;
    tf.units_reported      = keep.units_reported;
    tf.coords_reported     = keep.coords_reported;
    tf.getintegerv_fail    = keep.getintegerv_fail;
    tf.client_unit_fail    = keep.client_unit_fail;
    tf.client_unit_forced  = keep.client_unit_forced;
    tf.client_unit_answer  = keep.client_unit_answer;
    shader_seq = 0;
    program_seq = 0;
    buffer_seq = 0;
    /* The names the old context handed out are gone with it. They stay out of
     * the registry so using one is a violation rather than a lookup. */
    memset(bufs, 0, sizeof bufs);
    nbufs = 0;
    /* AND THE OBJECT LEDGERS EMPTY WITH IT, for the same reason: an object of a
     * destroyed context is not a leak of the new one, and a name from it is a
     * stale name, which checkname already records. The lifetime claim is
     * therefore per context, which is the scope the module's own acquire and
     * teardown work in. */
    nlive_sh = 0;
    nlive_pr = 0;
    obj_publish();
}

GLint sl_bgtfgl_loc(const char *name)
{
    int i;
    for (i = 0; i < BGTFGL_UNIFORMS; i++)
        if (strcmp(name, sl_bgtfgl_uniform_names[i]) == 0) return BGTFGL_LOC_BASE + i;
    return -1;
}

static int buf_index(GLuint name)
{
    int i;
    for (i = 0; i < nbufs; i++) if (bufs[i].name == name) return i;
    return -1;
}

/* How much of a buffer this stub can actually hold bytes for. A pool larger
 * than the cap has NO store, which is deliberate: the readback then writes
 * nothing, which is a failure shape the module has to handle rather than a
 * fixture that silently allocates whatever a test asked for. */
static long buf_store(long bytes)
{
    if (bytes < 0L) return 0L;
    return bytes <= (long) BGTFGL_POOL_CAP ? bytes : 0L;
}

GLuint sl_bgtfgl_make_buffer(long bytes)
{
    GLuint n = BGTFGL_MINT(0x400 | ++buffer_seq);
    if (nbufs < BGTFGL_BUFFERS) {
        bufs[nbufs].name = n;
        bufs[nbufs].bytes = bytes;
        bufs[nbufs].store = buf_store(bytes);
        memset(bufs[nbufs].data, 0, sizeof bufs[nbufs].data);
        nbufs++;
    }
    return n;
}

long sl_bgtfgl_buffer_bytes(GLuint name)
{
    const int i = buf_index(name);
    return i < 0 ? -1L : bufs[i].bytes;
}

int sl_bgtfgl_pool_write(GLuint name, long off, const unsigned char *src, long n)
{
    const int i = buf_index(name);
    if (i < 0 || off < 0L || n < 0L) return 0;
    if (off > bufs[i].store || n > bufs[i].store - off) {
        /* A TEST MAY NOT INSTALL MORE THAN THE MODULE SIZED. Allowing it would
         * let a case "prove" the record carries bytes the pool never had. */
        tf.v_pool_write_past_end++;
        return 0;
    }
    if (n != 0L && src != NULL) memcpy(bufs[i].data + off, src, (size_t) n);
    return 1;
}

int sl_bgtfgl_pool_read(GLuint name, long off, unsigned char *dst, long n)
{
    const int i = buf_index(name);
    if (i < 0 || off < 0L || n < 0L) return 0;
    if (off > bufs[i].store || n > bufs[i].store - off) return 0;
    if (n != 0L && dst != NULL) memcpy(dst, bufs[i].data + off, (size_t) n);
    return 1;
}

void sl_bgtfgl_test_state(GLuint prog, GLuint arr, GLuint elem, GLuint tfb, int discard)
{
    tf.current = prog;
    tf.arr_buf = arr;
    tf.elem_buf = elem;
    tf.tf_buf = tfb;
    tf.discard = discard != 0;
}

/* INHERITED CLIENT-ARRAY STATE, INSTALLED DIRECTLY. Not a GL call and not
 * counted: "the arrays came back exactly as they were" needs something to be
 * false about, and the module's restore would read as a no-op against a context
 * with nothing enabled. `t` is the per-unit coordinate enables, `nt` how many of
 * them are given, and the rest are the single arrays in the struct's own order.
 * The position array is included so a case can prove it was never moved. */
void sl_bgtfgl_test_client(GLint client_unit, const GLboolean *t, int nt,
                           GLboolean v, GLboolean c, GLboolean n, GLboolean i,
                           GLboolean e, GLboolean s, GLboolean f)
{
    int u;
    tf.client_unit = client_unit;
    for (u = 0; u < BGTFGL_UNITCAP; u++)
        tf.cli_t[u] = (t != NULL && u < nt) ? t[u] : (GLboolean) GL_FALSE;
    tf.cli_v = v; tf.cli_c = c; tf.cli_n = n;
    tf.cli_i = i; tf.cli_e = e; tf.cli_s = s; tf.cli_f = f;
}

/* Every entry point passes through here: one total call count, and the
 * after-teardown watchdog in one place. */
static void enter(void)
{
    tf.calls++;
    if (tf.context_destroyed) tf.v_call_after_context_destroyed++;
    /* AND WHICH CONTEXT THE CALL LANDED ON, in the one place every entry point
     * passes through. A call with NO context current is a violation outright:
     * nothing in GL happens, and a module that made one has issued a command
     * into nowhere. A call on the auxiliary context is counted rather than
     * refused - that is the whole point of the control this models - and the
     * per-call attribution is stamped on the transcript record by `rec` below. */
    if (tf.ctx_cur < 0) tf.v_call_no_context++;
    else if (tf.ctx_cur > 0) tf.calls_on_aux++;
    /* AND WHETHER THE CALL CAME AFTER THE POINT THE PRODUCT WOULD HAVE STOPPED
     * EXISTING. The terminate seam below does not return in the product; this
     * fixture's does, so every call after it is one the product could never have
     * made. Counting them here - at the one funnel - is what turns "no GL call
     * follows a lost context" into a measurement over EVERY entry point rather
     * than over the few a case thought to look at. */
    if (tf.fatal_calls > 0L) tf.calls_after_fatal++;
}

/* A name the module hands back to GL must belong to the generation in force.
 * 0 is not a name - it is the fixed pipeline or the unbound state, and always
 * legal. */
static void checkname(GLuint n)
{
    if (n != 0u && BGTFGL_GEN(n) != tf.context) tf.v_stale_name++;
}

/* RAISE ONE GL ERROR FLAG ON THE CONTEXT THAT IS CURRENT. Per context, because
 * that is the property a check made on the wrong context would get wrong. The
 * store is bounded and a raise past it is dropped - a case asking for more flags
 * than the store holds is asking about the drain's bound, which is smaller. */
static void gl_raise(long code)
{
    const int ax = tf.ctx_cur > 0 ? 1 : 0;
    if (tf.nerr_pending[ax] < BGTFGL_ERRCAP)
        tf.err_pending[ax][tf.nerr_pending[ax]++] = code;
}

/* THE INJECTION POINT: a named command's call raises what the case asked for.
 *
 * CALLED FROM rec AND BEFORE ITS LOG-FULL EARLY RETURN, deliberately. The error
 * a driver raises does not depend on whether this fixture had room to write a
 * transcript record, and hanging the injection off a successful record would
 * make a long run's behaviour quietly different from a short one's.
 *
 * THE OCCURRENCE CURSOR COUNTS EVERY CALL OF THE KIND, on either context,
 * because a case names a command - "the error comes from the draw" - and the
 * context that command lands on is the module's choice and part of what is under
 * test. */
static void inject_check(int kind)
{
    if (tf.inject_error_on == 0 || tf.inject_error_on != kind) return;
    if (tf.inject_error_seen == tf.inject_error_after) {
        const long base = tf.inject_error_code != 0 ? tf.inject_error_code
                                                    : (long) GL_INVALID_OPERATION;
        int k = tf.inject_error_count > 0 ? tf.inject_error_count : 1;
        int i;
        for (i = 0; i < k; i++) gl_raise(base + (long) i);
    }
    tf.inject_error_seen++;
}

static struct sl_bgtfgl_call *rec(int kind)
{
    struct sl_bgtfgl_call *r;
    inject_check(kind);
    if (tf.nlog >= (long) BGTFGL_LOG) { tf.v_log_full++; return NULL; }
    r = &tf.log[tf.nlog++];
    memset(r, 0, sizeof *r);
    r->kind = kind;
    /* THE CONTEXT STAMP, SET HERE AND NOWHERE ELSE, so no entry point can
     * record a call without saying which context it reached. The memset above
     * would otherwise leave 0, which is the MAIN context and would be a silent
     * wrong answer rather than a missing one. */
    r->ctx = tf.ctx_cur;
    return r;
}

/* ---- the fake GL: the program ------------------------------------------- */

static GLuint APIENTRY tfs_CreateShader(GLenum type)
{
    enter();
    tf.create_shader++;
    if (type != GL_VERTEX_SHADER) return 0u;
    if (tf.create_shader_zero) return 0u;
    tf.last_shader = BGTFGL_MINT(++shader_seq);
    obj_born(live_sh, &nlive_sh, tf.last_shader);
    obj_publish();
    return tf.last_shader;
}

static void APIENTRY tfs_ShaderSource(GLuint sh, GLsizei count,
                                      const char **src, const GLint *len)
{
    enter();
    tf.shader_source++;
    checkname(sh);
    if (sh == 0u || sh != tf.last_shader) tf.v_source_without_shader++;
    /* THE TEXT IS RECORDED, NOT COMPILED. There is no GLSL compiler here, so
     * what a test can check is that the program text was handed over once, in
     * one string, with no explicit length (the module passes NULL, meaning
     * NUL-terminated). What the text COMPUTES is outside this fixture. */
    tf.last_source = (src != NULL && count > 0) ? src[0] : NULL;
    tf.last_source_count = count;
    (void) len;
}

static void APIENTRY tfs_CompileShader(GLuint sh)
{
    enter();
    tf.compile++;
    checkname(sh);
}

static void APIENTRY tfs_GetShaderiv(GLuint sh, GLenum pname, GLint *out)
{
    enter();
    tf.get_shaderiv++;
    checkname(sh);
    if (pname == GL_COMPILE_STATUS && out != NULL) *out = tf.fail_compile ? 0 : 1;
}

static void APIENTRY tfs_GetShaderInfoLog(GLuint sh, GLsizei cap, GLsizei *n, char *log)
{
    static const char text[] = "SYNTHETIC: the fake driver refused this shader";
    GLsizei k = (GLsizei) (sizeof text - 1u);
    enter();
    tf.get_shaderlog++;
    checkname(sh);
    if (log == NULL || cap <= 0) return;
    if (k > cap - 1) k = cap - 1;
    memcpy(log, text, (size_t) k);
    log[k] = '\0';
    if (n != NULL) *n = k;
}

static GLuint APIENTRY tfs_CreateProgram(void)
{
    enter();
    tf.create_program++;
    if (tf.create_program_zero) return 0u;
    tf.last_program = BGTFGL_MINT(0x800 | ++program_seq);
    obj_born(live_pr, &nlive_pr, tf.last_program);
    obj_publish();
    return tf.last_program;
}

static void APIENTRY tfs_AttachShader(GLuint prog, GLuint sh)
{
    enter();
    tf.attach++;
    checkname(prog);
    checkname(sh);
}

static void APIENTRY tfs_TransformFeedbackVaryings(GLuint prog, GLsizei n,
                                                   const char *const *names,
                                                   GLenum mode)
{
    enter();
    tf.tf_varyings++;
    checkname(prog);
    /* glTransformFeedbackVaryings TAKES EFFECT AT LINK TIME, so the order is
     * the property, not just the call: a program named after its link captures
     * nothing and fails later at glBeginTransformFeedback. */
    if (tf.link > 0) tf.varyings_after_link++;
    tf.tf_varying_prog = prog;
    tf.tf_varying_count = n;
    tf.tf_varying_mode = mode;
    tf.tf_varying0 = (names != NULL && n > 0) ? names[0] : NULL;
}

static void APIENTRY tfs_LinkProgram(GLuint prog)
{
    enter();
    tf.link++;
    checkname(prog);
    if (tf.tf_varyings > 0 && tf.tf_varying_prog == prog) tf.link_saw_varyings = 1;
}

static void APIENTRY tfs_GetProgramiv(GLuint prog, GLenum pname, GLint *out)
{
    enter();
    tf.get_programiv++;
    checkname(prog);
    if (pname == GL_LINK_STATUS && out != NULL) *out = tf.fail_link ? 0 : 1;
}

static void APIENTRY tfs_GetProgramInfoLog(GLuint prog, GLsizei cap, GLsizei *n, char *log)
{
    static const char text[] = "SYNTHETIC: the fake driver refused this link";
    GLsizei k = (GLsizei) (sizeof text - 1u);
    enter();
    tf.get_programlog++;
    checkname(prog);
    if (log == NULL || cap <= 0) return;
    if (k > cap - 1) k = cap - 1;
    memcpy(log, text, (size_t) k);
    log[k] = '\0';
    if (n != NULL) *n = k;
}

/* THE NEUTRAL-PROGRAM CONTROL'S ONE DELETE. It is a resolved GL 2.0 name rather
 * than a symbol in this file, and it is gated by provide_delete_program, so the
 * link-time absence argument in the note at the top of this file is unchanged
 * for every OTHER delete: glDeleteShader and glDeleteBuffers still exist neither
 * as symbols nor in any table, and a module reaching for one gets NULL.
 *
 * THE MODELLED STATE IS NOT TOUCHED, which is GL's own behaviour: deleting a
 * program that is current FLAGS it and frees it when it stops being current, so
 * `tf.current` is left exactly as it was. The case where the deleted name IS the
 * current one is recorded as a note instead - this module's teardown runs with no
 * program current, so a raised note means the teardown moved. */
static void APIENTRY tfs_DeleteProgram(GLuint prog)
{
    struct sl_bgtfgl_call *r;
    enter();
    tf.delete_program++;
    checkname(prog);
    tf.last_deleted_program = prog;
    if (prog != 0u && prog == tf.current) tf.v_delete_current_program++;
    /* THE LEDGER, AND WHAT A MISS MEANS. A name that is not live was either
     * already deleted - the first delete removed it - or never minted here.
     * Both are recorded, and the double delete separately, because "deleted
     * exactly once" is the claim and a surplus call is not the same defect as a
     * fabricated name. */
    if (!obj_died(live_pr, &nlive_pr, prog)) {
        tf.v_delete_unknown_program++;
        /* A PROGRAM NAME THIS CONTEXT MINTED carries the 0x800 bit and a
         * sequence number already handed out, which is what separates "deleted
         * twice" from "a name that was never minted here". */
        if (prog != 0u && BGTFGL_GEN(prog) == tf.context
            && (prog & 0x800u) != 0u
            && (long) (prog & 0x7FFu) <= (long) program_seq
            && (long) (prog & 0x7FFu) >= 1L)
            tf.v_double_delete++;
    }
    obj_publish();
    r = rec(TFGL_DELETEPROGRAM);
    if (r != NULL) {
        r->a = (long) prog;
        r->b = (prog != 0u && prog == tf.current) ? 1L : 0L;
    }
}

/* THE NEUTRAL-PROGRAM CONTROL'S SECOND DELETE, on exactly the terms of the
 * first: a resolved GL 2.0 name gated by `provide_delete_shader`, never a
 * symbol in this file, so a module calling glDeleteShader as a direct core name
 * would still be an undefined symbol at link time and glDeleteBuffers still
 * exists neither as a symbol nor in any table.
 *
 * THE ATTACHED CASE IS NOT MODELLED AS A DELAY, and the reason belongs here
 * rather than in a test: GL frees a flagged shader when the last program holding
 * it goes away, so a shim that modelled the delay would have to model attachment
 * and detachment to say WHEN. What the module's claim needs is weaker and
 * exactly checkable - that the delete was ISSUED once per created shader and
 * never twice - so the ledger retires the name on the call and a second call on
 * it is a double delete. The flagged-but-alive window is therefore outside what
 * this fixture speaks for. */
static void APIENTRY tfs_DeleteShader(GLuint sh)
{
    struct sl_bgtfgl_call *r;
    enter();
    tf.delete_shader++;
    checkname(sh);
    tf.last_deleted_shader = sh;
    if (!obj_died(live_sh, &nlive_sh, sh)) {
        tf.v_delete_unknown_shader++;
        /* A SHADER NAME THIS CONTEXT MINTED carries no 0x800 bit - that range
         * is the programs' - and a sequence number already handed out. */
        if (sh != 0u && BGTFGL_GEN(sh) == tf.context
            && (sh & 0x800u) == 0u
            && (long) (sh & 0x7FFu) <= (long) shader_seq
            && (long) (sh & 0x7FFu) >= 1L)
            tf.v_double_delete++;
    }
    obj_publish();
    r = rec(TFGL_DELETESHADER);
    if (r != NULL) r->a = (long) sh;
}

/* THE AUXILIARY-CONTEXT CONTROL'S ONE QUERY (SL_BG_TF_AUX_EMPTY), on exactly
 * the terms the two deletes above are on: a resolved GL 2.0 name gated by
 * `provide_is_program`, never a symbol in this file, so a module calling
 * glIsProgram as a direct core name would be an undefined symbol at link time
 * and the absence argument at the top of this file is unchanged for every
 * read-back, fence, sync and query entry point.
 *
 * THE DEFAULT ANSWER IS TRUE FOR A NAME THIS SHIM MINTED, which models a
 * platform that honoured the sharing request: the object ledger already knows
 * which program names are live, so the answer is read from it rather than
 * invented. `is_program_false` forces GL_FALSE instead, which is the shape of a
 * context that does NOT share object names and the case the control must refuse
 * on.
 *
 * AND WHERE IT WAS ASKED IS RECORDED. The question is only meaningful on the
 * AUXILIARY context - asking it on the main one would answer about the context
 * that minted the name and prove nothing - so the current context at the call is
 * kept, and a case requires it to be the auxiliary one. */
/* THE PER-CONTEXT POINTER IDENTITY, ENFORCED AT THE CALL.
 *
 * Each of the two entry points the auxiliary-context control needs exists TWICE
 * in this file: the function the resolver hands back on the main context, and
 * the one it hands back on the auxiliary context. They do the same work; what
 * differs is that each knows which context it belongs to, and either one invoked
 * while the other's context is current raises v_ptr_wrong_context.
 *
 * WHY THIS IS THE ONLY WAY TO ASSERT IT. "The pointer called on the auxiliary
 * context was resolved on the auxiliary context" is a property of the POINTER'S
 * ORIGIN. It is not in the call's arguments, not in its ordering and not in any
 * count, so no transcript assertion can reach it; on a real driver the symptom
 * is a crash on some machines and nothing at all on others. Handing back
 * distinguishable pointers and checking the current context inside each is what
 * makes the property observable in a fixture.
 *
 * ctx_belongs_aux IS THE POINTER'S OWN CONTEXT, passed by its wrapper, not read
 * from anywhere. 0 main, 1 auxiliary. */
/* THE N-CALLS-THEN-FAIL GATE, declared here because two of the real-prepass
 * mode's own answers below use it and its definition sits with the context
 * functions at the end of this file. */
static int gate_bites(int on, long *after);

static void ptr_context_check(int ctx_belongs_aux)
{
    const int on_aux = tf.ctx_cur > 0 ? 1 : 0;
    if (tf.ctx_cur < 0) return;        /* no context: the funnel's own violation */
    if (on_aux != ctx_belongs_aux) tf.v_ptr_wrong_context++;
}

static GLboolean is_program_body(GLuint prog, int owner_aux)
{
    struct sl_bgtfgl_call *r;
    int i, live = 0;
    GLboolean ans;
    enter();
    ptr_context_check(owner_aux);
    tf.is_program_calls++;
    tf.is_program_last = prog;
    tf.is_program_on_aux = tf.ctx_cur > 0 ? 1 : 0;
    checkname(prog);
    for (i = 0; i < nlive_pr; i++) if (live_pr[i] == prog) live = 1;
    /* AND THE N-THEN-FAIL FORM OF THE SAME GATE, which is what lets the SHARED
     * acquire's share check pass and the real-prepass mode's own re-ask fail
     * (SL_BG_TF_AUX_REAL). */
    if (tf.is_program_false_after >= 0) {
        const GLboolean modelled = (prog != 0u && prog == tf.last_program)
                                   ? (GLboolean) GL_TRUE : (GLboolean) GL_FALSE;
        ans = gate_bites(1, &tf.is_program_false_after) ? (GLboolean) GL_FALSE
                                                        : modelled;
        r = rec(TFGL_ISPROGRAM);
        if (r != NULL) { r->a = (long) prog; r->b = (long) ans; }
        return ans;
    }
    ans = tf.is_program_false ? (GLboolean) GL_FALSE
                              : (GLboolean) (live ? GL_TRUE : GL_FALSE);
    r = rec(TFGL_ISPROGRAM);
    if (r != NULL) { r->a = (long) prog; r->b = ans ? 1L : 0L; }
    return ans;
}

static void use_program_body(GLuint prog, int owner_aux)
{
    struct sl_bgtfgl_call *r;
    enter();
    ptr_context_check(owner_aux);
    tf.use_program++;
    checkname(prog);
    /* THE API'S OWN RULE, MODELLED AND NOT RELAXED. glUseProgram inside a
     * begin/end scope is invalid, so a change made while feedback is active is
     * recorded as a violation rather than quietly applied. Counted BEFORE the
     * state moves, so the condition read is the one at the call. */
    if (tf.tf_active) tf.v_program_change_in_tf++;
    tf.current = prog;
    r = rec(TFGL_USEPROGRAM);
    if (r != NULL) r->a = (long) prog;
}

/* THE FOUR POINTERS: ONE PAIR PER CONTEXT, THE SAME WORK IN EACH.
 *
 * The body is shared, so the transcript, the counts and the object ledger are
 * identical whichever pointer was used - which is what keeps every existing
 * assertion about the sequence true - and the ONE difference is the context each
 * pointer belongs to, passed as a constant at the call rather than read from
 * anywhere. Four distinct addresses is what makes them distinguishable at all:
 * the resolver picks by the current context, so a module that resolves on one
 * context and calls on the other invokes the wrong pointer of a pair and
 * v_ptr_wrong_context rises. */
/* WHICH POINTER OF A PAIR IS EXECUTING, 0 main and 1 auxiliary
 * (SL_BG_TF_AUX_REAL).
 *
 * WHY A VARIABLE RATHER THAN A PARAMETER. The real-prepass mode calls ELEVEN
 * resolved entry points on the auxiliary context, and all but three of them
 * already exist here with bodies the main path's assertions read. Giving each
 * body an owner parameter would mean editing every main-context call site in
 * this file for a fact none of them is about; the auxiliary wrapper instead sets
 * this, delegates to the one body, and clears it, so the body asks
 * ptr_context_check the one question that distinguishes the pair and nothing
 * else changes. Single-threaded by construction: this fixture has one thread and
 * makes no nested entry-point call.
 *
 * THE GUARD IS AS STRONG AS THE FOUR HAND-WRITTEN PAIRS ABOVE. The addresses are
 * still distinct per name per context - the resolver picks by the current
 * context - so a module that resolves on one and calls on the other invokes the
 * wrong pointer and v_ptr_wrong_context rises. */
static int tfs_owner_aux;

static GLboolean APIENTRY tfs_IsProgram(GLuint prog)
{
    return is_program_body(prog, 0);
}

static GLboolean APIENTRY tfs_IsProgram_aux(GLuint prog)
{
    return is_program_body(prog, 1);
}

static void APIENTRY tfs_UseProgram(GLuint prog)
{
    use_program_body(prog, 0);
}

static void APIENTRY tfs_UseProgram_aux(GLuint prog)
{
    use_program_body(prog, 1);
}

static GLint APIENTRY tfs_GetUniformLocation(GLuint prog, const char *name)
{
    enter();
    tf.get_uniform_location++;
    checkname(prog);
    if (name == NULL) return -1;
    if (tf.withhold_uniform != NULL && strcmp(name, tf.withhold_uniform) == 0) return -1;
#ifdef BGTF_MUT_LOCSHIFT
    {
        const GLint l = sl_bgtfgl_loc(name);
        return l < 0 ? l : l + 1;
    }
#else
    return sl_bgtfgl_loc(name);
#endif
}

/* THE UNIFORM RULE THE REAL GL HAS, modelled rather than relaxed: glUniform*
 * writes the program IN USE, so with no program current it raises
 * INVALID_OPERATION and writes nothing, and a location of -1 is silently
 * ignored. Both are counted as rejected and leave the log alone, so a test
 * reads the log as "what the program actually received". The transcript records
 * the call either way, with the landing program 0 when GL refused it. */
static struct sl_bgtfgl_uwrite *accept(int kind, GLint loc)
{
    struct sl_bgtfgl_uwrite *w;
    struct sl_bgtfgl_call *r;
    /* WHICH POINTER OF THE PAIR WROTE IT (SL_BG_TF_AUX_REAL): the four uniform
     * writers share this one admission point, so the per-context identity guard
     * is asked once here rather than in each of them. */
    ptr_context_check(tfs_owner_aux);
    tf.uniform_calls++;
    r = rec(TFGL_UNIFORM);
    if (r != NULL) { r->a = (long) loc; r->b = (long) tf.current; }
    if (tf.current == 0u || loc < 0 || tf.nu >= BGTFGL_UMAX) {
        tf.uniform_rejected++;
        if (r != NULL) r->b = 0;
        return NULL;
    }
    w = &tf.u[tf.nu++];
    memset(w, 0, sizeof *w);
    w->kind = kind;
    w->loc = loc;
    w->prog = tf.current;
    return w;
}

static void APIENTRY tfs_Uniform1f(GLint loc, GLfloat v)
{
    struct sl_bgtfgl_uwrite *w;
    enter();
    w = accept(BGTF_U1F, loc);
    if (w != NULL) { w->count = 1; w->fv[0] = v; }
}

static void APIENTRY tfs_Uniform3f(GLint loc, GLfloat a, GLfloat b, GLfloat c)
{
    struct sl_bgtfgl_uwrite *w;
    enter();
    w = accept(BGTF_U3F, loc);
    if (w != NULL) { w->count = 3; w->fv[0] = a; w->fv[1] = b; w->fv[2] = c; }
}

static void APIENTRY tfs_Uniform1i(GLint loc, GLint v)
{
    struct sl_bgtfgl_uwrite *w;
    enter();
    w = accept(BGTF_U1I, loc);
    if (w != NULL) { w->count = 1; w->ival = v; }
}

static void APIENTRY tfs_Uniform1fv(GLint loc, GLsizei n, const GLfloat *v)
{
    struct sl_bgtfgl_uwrite *w;
    GLsizei i;
    enter();
    w = accept(BGTF_U1FV, loc);
    if (w == NULL) return;
    w->count = n;
    for (i = 0; i < n && i < 16; i++) w->fv[i] = v[i];
}

/* ---- the fake GL: buffers and transform feedback ------------------------ */

static void APIENTRY tfs_GenBuffers(GLsizei n, GLuint *out)
{
    struct sl_bgtfgl_call *r;
    GLsizei i;
    enter();
    tf.gen_buffers++;
    r = rec(TFGL_GENBUFFERS);
    if (r != NULL) r->a = (long) n;
    if (out == NULL) return;
    for (i = 0; i < n; i++) {
        if (tf.gen_buffers_zero) { out[i] = 0u; continue; }
        out[i] = sl_bgtfgl_make_buffer(0);
        tf.generated++;
    }
    if (r != NULL && n > 0) r->b = (long) out[0];
}

static void APIENTRY tfs_BindBuffer(GLenum target, GLuint name)
{
    struct sl_bgtfgl_call *r;
    enter();
    ptr_context_check(tfs_owner_aux);
    tf.bind_buffer++;
    checkname(name);
    if (name != 0u && buf_index(name) < 0) tf.v_stale_name++;
    if (target == GL_ARRAY_BUFFER) {
#ifdef BGTF_MUT_ARRBUF
        if (name != 0u) tf.arr_buf = name;   /* the unbind does nothing */
#else
        tf.arr_buf = name;
#endif
    }
    else if (target == GL_ELEMENT_ARRAY_BUFFER) tf.elem_buf = name;
    else tf.v_unknown_enum++;
    r = rec(TFGL_BINDBUFFER);
    if (r != NULL) { r->a = (long) target; r->b = (long) name; }
}

/* THE REQUEST AND THE STORAGE ARE TWO DIFFERENT NUMBERS, which is the fact the
 * module's allocation check exists for and the one a stub that always installs
 * the request cannot express. glBufferData is void: a driver that will not
 * honour a size reports nothing and leaves the object at whatever it had, so
 * "the call returned" says nothing about whether storage exists. The installed
 * size is therefore chosen here, independently of `size`, and GL_BUFFER_SIZE
 * below is the only way anything downstream can learn which it got. */
static void APIENTRY tfs_BufferData(GLenum target, ptrdiff_t size,
                                    const GLvoid *data, GLenum usage)
{
    struct sl_bgtfgl_call *r;
    long installed;
    int i, initial;
    enter();
    tf.buffer_data++;
    if (target != GL_ARRAY_BUFFER && target != GL_ELEMENT_ARRAY_BUFFER
        && target != GL_TRANSFORM_FEEDBACK_BUFFER)
        tf.v_unknown_enum++;
    /* The size lands on the name bound to the target, which is what a real
     * implementation does and what makes the overrun watchdog meaningful. */
    i = buf_index(target == GL_ARRAY_BUFFER ? tf.arr_buf : tf.elem_buf);
    /* AN INITIAL ALLOCATION IS ONE ONTO AN OBJECT THAT HAS NO STORAGE YET, and
     * a growth is one onto an object that already has some. The module only
     * reaches glBufferData when its recorded capacity is short, so those are
     * exactly its two allocation sites and they are injectable apart. */
    initial = (i < 0) || bufs[i].bytes == 0L;
    installed = (long) size;
    if (initial ? tf.alloc_short_initial : tf.alloc_short_growth) {
        installed = tf.alloc_bytes;
        tf.alloc_shorted++;
    }
    if (i >= 0) {
        if (tf.alloc_install_none) tf.alloc_shorted++;   /* the object is left as found */
        else {
            bufs[i].bytes = installed;
            /* A REALLOCATION LOSES THE OLD CONTENTS, as GL's does: glBufferData
             * replaces the storage, and a test that found a previous item's
             * bytes still readable here would be measuring the stub. */
            bufs[i].store = buf_store(installed);
            memset(bufs[i].data, 0, sizeof bufs[i].data);
        }
    }
    r = rec(TFGL_BUFFERDATA);
    if (r != NULL) {
        r->a = (long) target;
        r->b = (long) size;
        r->c = (long) usage;
        r->d = data != NULL ? 1L : 0L;
        r->off = (i >= 0) ? bufs[i].bytes : -1L;
    }
}

/* ALLOCATION ACCEPTANCE, asked once per allocation. A GL_BUFFER_SIZE read is
 * metadata about an object - no pipeline synchronisation, no contents - which
 * is why it is modelled here while no read-back, fence or query entry point
 * exists in this stub at all.
 *
 * A QUERY THAT WRITES NOTHING IS ITS OWN FAILURE SHAPE. Real GL leaves the out
 * parameter untouched on an error, so a caller that initialised it to a
 * sentinel keeps the sentinel; `getbufparam_fail` models exactly that, and it
 * is kept separate from a short report because "storage is smaller than asked"
 * and "I cannot tell you how large the storage is" are different facts that a
 * correct module must refuse for different reasons. */
static void APIENTRY tfs_GetBufferParameteriv(GLenum target, GLenum pname, GLint *out)
{
    struct sl_bgtfgl_call *r;
    long have;
    int i;
    enter();
    tf.get_buffer_parameteriv++;
    if (target != GL_ARRAY_BUFFER && target != GL_ELEMENT_ARRAY_BUFFER
        && target != GL_TRANSFORM_FEEDBACK_BUFFER)
        tf.v_unknown_enum++;
    if (pname != GL_BUFFER_SIZE) tf.v_unknown_enum++;
    i = buf_index(target == GL_ARRAY_BUFFER ? tf.arr_buf : tf.elem_buf);
    have = (i >= 0) ? bufs[i].bytes : 0L;
    if (tf.getbufparam_bytes >= 0) have = tf.getbufparam_bytes;
#ifdef BGTF_MUT_ALLOCOK
    /* THE DRIVER THAT LIES. The last glBufferData's REQUEST is reported rather
     * than what was installed, so no allocation can ever be seen to fail. */
    {
        long k;
        for (k = tf.nlog - 1; k >= 0; k--)
            if (tf.log[k].kind == TFGL_BUFFERDATA) { have = tf.log[k].b; break; }
    }
#endif
    r = rec(TFGL_GETBUFPARAM);
    if (r != NULL) {
        r->a = (long) target;
        r->b = (long) pname;
        r->c = tf.getbufparam_fail ? -1L : have;
        r->d = tf.getbufparam_fail ? 1L : 0L;
    }
    if (tf.getbufparam_fail) return;         /* nothing written: the caller keeps its own */
    if (out != NULL) *out = (GLint) have;
}

static void APIENTRY tfs_BindBufferBase(GLenum target, GLuint index, GLuint name)
{
    struct sl_bgtfgl_call *r;
    enter();
    ptr_context_check(tfs_owner_aux);
#ifndef BGTF_MUT_SONOTFBIND
    tf.bind_buffer_base++;
#endif
    checkname(name);
    if (name != 0u && buf_index(name) < 0) tf.v_stale_name++;
    if (target != GL_TRANSFORM_FEEDBACK_BUFFER) tf.v_unknown_enum++;
    else if (index == 0u) tf.tf_buf = name;
    else tf.v_unknown_enum++;
    r = rec(TFGL_BINDBUFFERBASE);
    if (r != NULL) { r->a = (long) target; r->b = (long) index; r->c = (long) name; }
}

static void APIENTRY tfs_BeginTransformFeedback(GLenum mode)
{
    struct sl_bgtfgl_call *r;
    enter();
    ptr_context_check(tfs_owner_aux);
    tf.begin_tf++;
    if (tf.tf_active) tf.v_tf_nested++;
    if (tf.tf_buf == 0u) tf.v_draw_without_tf_buffer++;
    if (tf.current == 0u) tf.v_draw_without_program++;
    tf.tf_active = 1;
    r = rec(TFGL_BEGINTF);
    if (r != NULL) r->a = (long) mode;
}

static void APIENTRY tfs_EndTransformFeedback(void)
{
    enter();
    ptr_context_check(tfs_owner_aux);
    tf.end_tf++;
    if (!tf.tf_active) tf.v_tf_unbalanced++;
    tf.tf_active = 0;
    (void) rec(TFGL_ENDTF);
}

/* ---- the fake GL: the REAL-PREPASS MODE's three own names ----------------
 *
 * NONE OF THEM IS A SYMBOL IN THIS FILE UNDER ITS GL NAME, so a module calling
 * glIsBuffer, glGenVertexArrays or glBindVertexArray directly rather than
 * through the resolver would be an undefined symbol at LINK time and the fixture
 * would not build. All three are gated on `provide_real_set`, so the default
 * fixture answers NULL for each - which is the shape of a context that cannot
 * supply the set and the case the mode must refuse on.
 *
 * glIsBuffer IS A QUERY ABOUT A NAME, not a read-back of contents and not a
 * synchronisation point, so the link-time absence of every read-back, fence and
 * sync entry point is exactly as strong as it was. */
static GLboolean is_buffer_body(GLuint name, int owner_aux)
{
    struct sl_bgtfgl_call *r;
    GLboolean ans;
    enter();
    ptr_context_check(owner_aux);
    tf.is_buffer_calls++;
    tf.is_buffer_last = name;
    tf.is_buffer_on_aux = tf.ctx_cur > 0 ? 1 : 0;
    /* THE ANSWER IS INVENTED. A name this stub minted in the current generation
     * is a buffer; anything else is not - which is the model of a name that
     * means nothing in the context being asked, the shape an UNSHARED buffer
     * object has from a second context. `is_buffer_false` forces GL_FALSE
     * instead, and `is_buffer_false_after` lets N questions pass first so the
     * INPUT buffer's check and the OUTPUT pool's check are failable apart. */
    ans = (name != 0u && buf_index(name) >= 0) ? (GLboolean) GL_TRUE
                                               : (GLboolean) GL_FALSE;
    if (gate_bites(tf.is_buffer_false, &tf.is_buffer_false_after))
        ans = (GLboolean) GL_FALSE;
    r = rec(TFGL_ISBUFFER);
    if (r != NULL) { r->a = (long) name; r->b = (long) ans; }
    return ans;
}

static GLboolean APIENTRY tfs_IsBuffer(GLuint name)
{
    return is_buffer_body(name, 0);
}

static GLboolean APIENTRY tfs_IsBuffer_aux(GLuint name)
{
    return is_buffer_body(name, 1);
}

/* A VERTEX ARRAY OBJECT IS A CONTAINER OBJECT AND IS NOT SHARED, which is the
 * one fact these two exist to model. The bound object is kept PER CONTEXT -
 * `vao_bound` for the auxiliary one and `vao_bound_main` for the main one - so a
 * bind that landed on the wrong context is visible as state and not only as a
 * pointer-identity violation. The names are minted in the current generation
 * like every other, so one used after a context recreation is stale. */
static void vao_gen_body(GLsizei n, GLuint *out, int owner_aux)
{
    struct sl_bgtfgl_call *r;
    enter();
    ptr_context_check(owner_aux);
    tf.gen_vao++;
    if (tf.ctx_cur == 0) tf.v_vao_on_main++;
    if (out == NULL || n <= 0) return;
    out[0] = tf.gen_vao_zero ? 0u : BGTFGL_MINT(0x800 + (int) tf.gen_vao);
    tf.last_gen_vao = out[0];
    r = rec(TFGL_GENVAO);
    if (r != NULL) { r->a = (long) n; r->b = (long) out[0]; }
}

static void vao_bind_body(GLuint name, int owner_aux)
{
    struct sl_bgtfgl_call *r;
    enter();
    ptr_context_check(owner_aux);
    tf.bind_vao++;
    if (tf.ctx_cur == 0) tf.v_vao_on_main++;
    checkname(name);
    if (tf.ctx_cur > 0) tf.vao_bound = name;
    else                tf.vao_bound_main = name;
    r = rec(TFGL_BINDVAO);
    if (r != NULL) r->a = (long) name;
}

static void APIENTRY tfs_GenVertexArrays(GLsizei n, GLuint *out)
{
    vao_gen_body(n, out, 0);
}

static void APIENTRY tfs_GenVertexArrays_aux(GLsizei n, GLuint *out)
{
    vao_gen_body(n, out, 1);
}

static void APIENTRY tfs_BindVertexArray(GLuint name)
{
    vao_bind_body(name, 0);
}

static void APIENTRY tfs_BindVertexArray_aux(GLuint name)
{
    vao_bind_body(name, 1);
}

/* ---- the fake GL: the AUXILIARY twins of the eight names the real-prepass
 *      mode calls on the second context -------------------------------------
 *
 * Each sets `tfs_owner_aux`, delegates to the ONE body the main path already
 * uses, and clears it. So the transcript, the counts, the modelled state and the
 * object ledger are identical whichever pointer of a pair was used - which keeps
 * every existing assertion about the sequence true - and the one difference is
 * the context each pointer is valid on. */
#define TFS_AUX_THUNK(decl, call)                                             \
    static void APIENTRY decl                                                 \
    { tfs_owner_aux = 1; call; tfs_owner_aux = 0; }

TFS_AUX_THUNK(tfs_BindBuffer_aux(GLenum t, GLuint n), tfs_BindBuffer(t, n))
TFS_AUX_THUNK(tfs_BindBufferBase_aux(GLenum t, GLuint i, GLuint n),
              tfs_BindBufferBase(t, i, n))
TFS_AUX_THUNK(tfs_BeginTransformFeedback_aux(GLenum m),
              tfs_BeginTransformFeedback(m))
TFS_AUX_THUNK(tfs_EndTransformFeedback_aux(void), tfs_EndTransformFeedback())
TFS_AUX_THUNK(tfs_Uniform1f_aux(GLint l, GLfloat v), tfs_Uniform1f(l, v))
TFS_AUX_THUNK(tfs_Uniform3f_aux(GLint l, GLfloat a, GLfloat b, GLfloat c),
              tfs_Uniform3f(l, a, b, c))
TFS_AUX_THUNK(tfs_Uniform1i_aux(GLint l, GLint v), tfs_Uniform1i(l, v))
TFS_AUX_THUNK(tfs_Uniform1fv_aux(GLint l, GLsizei n, const GLfloat *v),
              tfs_Uniform1fv(l, n, v))

/* ---- the fake GL: the diagnostic's two, resolvable only on request ------
 *
 * NEITHER IS NAMED glGetBufferSubData OR glGetError AS A SYMBOL. The functions
 * are tfs_*, handed out by sl_gl_proc when a test asks, so the link-time proof
 * that the module makes no DIRECT call to a read-back or a synchronisation
 * point is exactly as strong as it was.
 *
 * THE READ IS MODELLED THE WAY A REAL ONE FAILS. A read that writes nothing
 * leaves the destination as the caller prepared it - which is why the module
 * fills the destination with a sentinel first and why the three shapes below
 * are kept apart: nothing written, a short write whose tail is untouched, and a
 * full write of zeros. The third is the dangerous one, because zeros look like
 * data and compare equal to nothing. */
static void APIENTRY tfs_GetBufferSubData(GLenum target, ptrdiff_t off,
                                          ptrdiff_t size, GLvoid *out)
{
    struct sl_bgtfgl_call *r;
    const GLuint name = (target == GL_ARRAY_BUFFER) ? tf.arr_buf
                      : (target == GL_ELEMENT_ARRAY_BUFFER) ? tf.elem_buf : 0u;
    long have, wrote = 0L, want = (long) size;
    enter();
    tf.get_buffer_sub_data++;
    tf.get_buffer_sub_bytes += want;
    if (target != GL_ARRAY_BUFFER && target != GL_ELEMENT_ARRAY_BUFFER
        && target != GL_TRANSFORM_FEEDBACK_BUFFER)
        tf.v_unknown_enum++;
    /* A READ INSIDE AN OPEN TRANSFORM-FEEDBACK SCOPE is the placement error the
     * whole design is about: the pool is not finished being written. */
    if (tf.tf_active) tf.v_read_inside_tf++;
    have = sl_bgtfgl_buffer_bytes(name);
    if (have < 0L || off < 0L || want < 0L || off > have || want > have - off)
        tf.v_read_past_end++;
    if (out != NULL && want > 0L && !tf.getsub_fail) {
        wrote = tf.getsub_bytes >= 0L && tf.getsub_bytes < want ? tf.getsub_bytes
                                                                : want;
        if (tf.getsub_zero) memset(out, 0, (size_t) wrote);
        else if (!sl_bgtfgl_pool_read(name, (long) off, (unsigned char *) out, wrote))
            wrote = 0L;                  /* no store behind this name */
    }
    r = rec(TFGL_GETBUFSUBDATA);
    if (r != NULL) {
        r->a = (long) target;
        r->b = want;
        r->c = wrote;
        r->d = (long) name;
        r->off = (long) off;
    }
}

/* ---- the fake GL: the core names, called directly ----------------------- */

/* glFinish, MODELLED AS A COUNTED, ORDERED NO-OP.
 *
 * WHAT IT CAN AND CANNOT SAY. This shim has no pipeline, so there is nothing
 * for a synchronisation point to wait on and the call does nothing but record
 * itself. What that records is the one thing a fixture can own: that the call
 * was ISSUED, exactly once per boundary, in its place in the ordered transcript.
 * Whether a driver's glFinish would change anything about any pixel is outside
 * this process entirely and is not claimed anywhere.
 *
 * IT IS NOT A READ-BACK. It returns nothing, reads nothing and writes nothing
 * through a pointer, so the shim's standing claim that no read-back entry point
 * exists here is untouched by its presence - see the note at the top of this
 * file for what the absence list still covers. */
void glFinish(void)
{
    enter();
    tf.finish_calls++;
    (void) rec(TFGL_FINISH);
}

/* glFlush, MODELLED AS A COUNTED, ORDERED NO-OP, SPLIT BY CONTEXT
 * (SL_BG_TF_AUX_SYNC).
 *
 * WHAT IT CAN AND CANNOT SAY. There is no pipeline here, so there is nothing to
 * flush and the call does nothing but record itself. What it records is the one
 * thing a fixture can own: that the PRODUCER'S OWN context was flushed, in its
 * place in the ordered transcript, exactly once per handoff - which is the
 * requirement OpenGL 4.5 core section 4.1.2 states and which
 * GL_SYNC_FLUSH_COMMANDS_BIT does not meet across contexts. Whether a real
 * driver's flush makes a fence reachable is outside this process entirely.
 *
 * IT IS NOT A READ-BACK AND NOT A WAIT. It returns nothing and the
 * specification requires it to return without waiting, so the shim's standing
 * claim that no read-back and no waiting entry point exists here is untouched by
 * its presence - see the note at the top of this file for what the absence list
 * still covers, the three sync entry points among them. */
void glFlush(void)
{
    enter();
    tf.flush_calls++;
    if (tf.ctx_cur > 0) tf.flush_calls_aux++;
    else if (tf.ctx_cur == 0) tf.flush_calls_main++;
    (void) rec(TFGL_FLUSH);
}

/* ---- the fake GL: the sync objects (SL_BG_TF_AUX_SYNC) ------------------
 *
 * THE HANDLES ARE INVENTED HERE AND ARE NOT POINTERS TO ANYTHING. A GLsync is
 * an opaque pointer type in OpenGL 4.6 section 4.1, so the only properties a
 * handle has that anyone may rely on are that a successful creation answers
 * something other than NULL and that the same value identifies the same object
 * until it is deleted. This shim mints values from a counter, casts them to
 * void *, and never dereferences one; the module does not either, and a module
 * that did would fault here rather than pass.
 *
 * THE TABLE IS WHAT MAKES A USE-AFTER-FREE VISIBLE. A wait or a delete naming a
 * handle that was never minted, or one already retired, is not distinguishable
 * from a correct call by any count - so every handle is kept with the context it
 * was created on and a live flag, and both lookups are checked.
 *
 * AND EVERY WAIT IS CHECKED FOR BEING A CROSS-CONTEXT WAIT. In this mode a wait
 * that happens on the context that minted the handle is a handoff that went
 * nowhere: legal GL, and useless. It is counted as v_sync_wait_own rather than
 * refused, because the shim's job is to make the property readable and the
 * assertion belongs to the cases. */
static long sync_seq;

static int sync_find(long h)
{
    int i;
    for (i = 0; i < tf.nsync_tab; i++) if (tf.sync_tab[i] == h) return i;
    return -1;
}

static void *sync_mint(void)
{
    const long h = 0x5C000000L + ++sync_seq;
    if (tf.nsync_tab < BGTFGL_SYNCS) {
        tf.sync_tab[tf.nsync_tab]      = h;
        tf.sync_tab_ctx[tf.nsync_tab]  = tf.ctx_cur;
        tf.sync_tab_live[tf.nsync_tab] = 1;
        tf.nsync_tab++;
    }
    tf.sync_created++;
    tf.sync_live++;
    if (tf.sync_live > tf.sync_peak) tf.sync_peak = tf.sync_live;
    tf.last_fence = h;
    tf.last_fence_on_aux = tf.ctx_cur > 0 ? 1 : 0;
    return (void *) h;
}

/* THE BODY BOTH CREATION POINTERS SHARE. `on_aux` is which context's function
 * was invoked, which `ax` then compares against the context that is CURRENT -
 * the per-context identity guard the program and uniform pairs use. */
static void *fence_body(int on_aux, GLenum condition, GLbitfield flags)
{
    const int ax = tf.ctx_cur > 0 ? 1 : 0;
    struct sl_bgtfgl_call *r;
    void *h;
    enter();
    if (ax != on_aux) tf.v_ptr_wrong_context++;
    if (on_aux) tf.fence_calls_aux++;
    else        tf.fence_calls_main++;
    /* THE TWO ARGUMENTS OpenGL 4.6 section 4.1.1 CONSTRAINS: the condition must
     * be SYNC_GPU_COMMANDS_COMPLETE and flags must be zero. A wrong pair is an
     * INVALID_ENUM or an INVALID_VALUE on a real driver; here it is recorded, so
     * a module that passed the wrong one is caught by a count rather than by a
     * driver nobody is running. 0x9117 is the registry's value. */
    if (condition != 0x9117u || flags != 0u) tf.v_sync_badfence++;
    if ((on_aux ? tf.fence_zero_aux : tf.fence_zero_main)
        && (on_aux ? tf.fence_zero_aux_after : tf.fence_zero_main_after) < 0L) {
        h = NULL;
    } else if ((on_aux ? tf.fence_zero_aux_after : tf.fence_zero_main_after) == 0L) {
        /* THE GATE BITES ONCE AND THEN STOPS, so a case can fail the SECOND
         * bracket's creation while letting the first through. */
        if (on_aux) tf.fence_zero_aux_after = -2L;
        else        tf.fence_zero_main_after = -2L;
        h = NULL;
    } else {
        if ((on_aux ? tf.fence_zero_aux_after : tf.fence_zero_main_after) > 0L) {
            if (on_aux) tf.fence_zero_aux_after--;
            else        tf.fence_zero_main_after--;
        }
        h = sync_mint();
    }
    r = rec(TFGL_FENCESYNC);
    if (r != NULL) {
        r->a = (long) h;
        r->b = (long) condition;
        r->c = (long) flags;
    }
    return h;
}

static void *APIENTRY tfs_FenceSync(GLenum condition, GLbitfield flags)
{
    return fence_body(0, condition, flags);
}

static void *APIENTRY tfs_FenceSync_aux(GLenum condition, GLbitfield flags)
{
    return fence_body(1, condition, flags);
}

static void wait_body(int on_aux, void *sync, GLbitfield flags,
                      unsigned long long timeout)
{
    const int ax = tf.ctx_cur > 0 ? 1 : 0;
    const long h = (long) sync;
    struct sl_bgtfgl_call *r;
    int i;
    enter();
    if (ax != on_aux) tf.v_ptr_wrong_context++;
    if (on_aux) tf.wait_calls_aux++;
    else        tf.wait_calls_main++;
    /* FLAGS MUST BE 0 AND THE TIMEOUT MUST BE TIMEOUT_IGNORED, which OpenGL 4.6
     * section 4.1.1 requires of WaitSync and makes an INVALID_VALUE otherwise.
     * 0xFFFFFFFFFFFFFFFF is the registry's value for TIMEOUT_IGNORED. */
    if (flags != 0u || timeout != 0xFFFFFFFFFFFFFFFFull) tf.v_sync_badwait++;
    i = sync_find(h);
    if (i < 0 || !tf.sync_tab_live[i]) tf.v_sync_unknown++;
    else if ((tf.sync_tab_ctx[i] > 0 ? 1 : 0) == ax) tf.v_sync_wait_own++;
    tf.last_wait = h;
    tf.last_wait_on_aux = ax;
    r = rec(TFGL_WAITSYNC);
    if (r != NULL) {
        r->a = h;
        r->b = (long) flags;
        r->c = timeout == 0xFFFFFFFFFFFFFFFFull ? 1L : 0L;
    }
}

static void APIENTRY tfs_WaitSync(void *sync, GLbitfield flags,
                                  unsigned long long timeout)
{
    wait_body(0, sync, flags, timeout);
}

static void APIENTRY tfs_WaitSync_aux(void *sync, GLbitfield flags,
                                      unsigned long long timeout)
{
    wait_body(1, sync, flags, timeout);
}

static void delete_body(int on_aux, void *sync)
{
    const int ax = tf.ctx_cur > 0 ? 1 : 0;
    const long h = (long) sync;
    struct sl_bgtfgl_call *r;
    int i;
    enter();
    if (ax != on_aux) tf.v_ptr_wrong_context++;
    if (on_aux) tf.delete_calls_aux++;
    else        tf.delete_calls_main++;
    i = sync_find(h);
    if (i < 0) {
        tf.v_sync_unknown++;
    } else if (!tf.sync_tab_live[i]) {
        tf.v_sync_double_delete++;
    } else {
        tf.sync_tab_live[i] = 0;
        tf.sync_deleted++;
        tf.sync_live--;
    }
    r = rec(TFGL_DELETESYNC);
    if (r != NULL) r->a = h;
}

static void APIENTRY tfs_DeleteSync(void *sync)    { delete_body(0, sync); }
static void APIENTRY tfs_DeleteSync_aux(void *sync) { delete_body(1, sync); }

/* glGetError IS A REAL SYMBOL HERE, and that is the core-call contract rather
 * than a convenience. The module calls it directly because it is a GL 1.0 core
 * export that wglGetProcAddress is documented to answer NULL for, so a direct
 * call is the only form in which the error state is observable on a capable
 * driver. It is not a read-back or a synchronisation point, so the link-time
 * absence of those is untouched.
 *
 * THE QUEUE IS WHY THE TWO READINGS ARE SEPARABLE: one value per call, in
 * order, then GL_NO_ERROR for ever - which is how a PRE-EXISTING error can be
 * modelled distinctly from one raised by the readback. `get_error_calls` is the
 * counter the off case asserts is zero. */
GLenum glGetError(void)
{
    struct sl_bgtfgl_call *r;
    long v = 0L;
    const int ax = tf.ctx_cur > 0 ? 1 : 0;
    enter();
    /* WHICH CONTEXT THE READ LANDED ON, counted before anything is retired. A
     * read CLEARS what it returns, so a check made on the wrong context both
     * misses that context's error and consumes another's - and the only way to
     * see the second half of that is to count the reads per context. */
    if (ax) tf.get_error_on_aux++;
    else    tf.get_error_on_main++;
    if (tf.get_error_calls < (long) (sizeof tf.geterror_queue
                                     / sizeof tf.geterror_queue[0])
        && tf.get_error_calls < (long) tf.ngeterror_queue) {
        /* THE OLDER QUEUE STILL ANSWERS FIRST, unchanged. It belongs to the
         * readback diagnostic, whose two reads are both on the MAIN context and
         * whose cases set it explicitly; with `ngeterror_queue` at its default 0
         * this branch is never taken and the flagged state below is the whole
         * model. */
        v = tf.geterror_queue[tf.get_error_calls];
    } else if (tf.nerr_pending[ax] > 0) {
        /* FIFO: the first code flagged is the first answered, so a drain reads
         * them in the order a driver set them. */
        int i;
        v = tf.err_pending[ax][0];
        if (!tf.inject_error_stuck) {
            for (i = 1; i < tf.nerr_pending[ax]; i++)
                tf.err_pending[ax][i - 1] = tf.err_pending[ax][i];
            tf.nerr_pending[ax]--;
        }
        /* `inject_error_stuck` RETIRES NOTHING, which is the implementation that
         * will not come back: every read answers the same nonzero code and the
         * caller's bound is the only thing that ends the loop. */
    }
    tf.get_error_calls++;
    r = rec(TFGL_GETERROR);
    if (r != NULL) { r->a = v; r->b = (long) ax; }
    return (GLenum) v;
}

/* ---- THE CLIENT-ARRAY STATE, MODELLED -----------------------------------
 *
 * The unit index comes from `client_unit` at the moment of the call rather than
 * from an argument, which is how GL works and is what makes a restore that
 * selects the wrong unit visible at all. A unit outside the modelled range is
 * refused and counted; it never writes out of bounds.
 */
static int cli_unit_index(void)
{
    const long u = (long) tf.client_unit - (long) GL_TEXTURE0;
    if (u < 0 || u >= BGTFGL_UNITCAP) { tf.v_client_unit_range++; return -1; }
    return (int) u;
}

/* The modelled enable a token names, or NULL if the stub does not model it. */
static GLboolean *cli_slot(GLenum cap)
{
    int u;
    switch (cap) {
    case GL_VERTEX_ARRAY:          return &tf.cli_v;
    case GL_COLOR_ARRAY:           return &tf.cli_c;
    case GL_NORMAL_ARRAY:          return &tf.cli_n;
    case GL_INDEX_ARRAY:           return &tf.cli_i;
    case GL_EDGE_FLAG_ARRAY:       return &tf.cli_e;
    case GL_SECONDARY_COLOR_ARRAY: return &tf.cli_s;
    case GL_FOG_COORD_ARRAY:       return &tf.cli_f;
    case GL_TEXTURE_COORD_ARRAY:
        u = cli_unit_index();
        return u < 0 ? NULL : &tf.cli_t[u];
    default:
        return NULL;
    }
}

GLboolean glIsEnabled(GLenum cap)
{
    struct sl_bgtfgl_call *r;
    const GLboolean *p;
    GLboolean v;
    enter();
    tf.is_enabled++;
    p = cli_slot(cap);
    if (p == NULL) {
        /* NOT A MODELLED CLIENT ARRAY. The server enables this stub does not
         * model answer FALSE, exactly as a query of unmodelled state must, and
         * the token is counted so a case cannot pass on an answer that meant
         * nothing. GL_RASTERIZER_DISCARD is deliberately not special-cased: the
         * module never asks about it. */
        if (cap != GL_RASTERIZER_DISCARD) tf.v_client_state_unknown++;
        v = (GLboolean) GL_FALSE;
    } else {
        v = *p;
    }
    r = rec(TFGL_ISENABLED);
    if (r != NULL) {
        r->a = (long) cap;
        r->b = (long) v;
        r->c = (long) tf.client_unit;
    }
    return v;
}

static void cli_set(GLenum cap, int on)
{
    struct sl_bgtfgl_call *r;
    GLboolean *p = cli_slot(cap);
    tf.client_state++;
    if (on) tf.client_enable++; else tf.client_disable++;
    /* AN ARRAY ENABLE IS PER-CONTEXT STATE, AND THE AUXILIARY CONTEXT'S IS KEPT
     * APART (SL_BG_TF_AUX_REAL). A client array enable is held in the bound
     * vertex array object, which is a CONTAINER object and so is never shared,
     * so a model with one set of fields would have the real-prepass mode's own
     * enable on the second context read as a change to the caller's state on the
     * first - and would make the position-array watchdog below fire on a mode
     * that never touched the main context's arrays at all.
     *
     * ONLY THE POSITION ARRAY IS MODELLED THERE, because it is the only one the
     * mode names on that context: anything else arriving on the auxiliary
     * context is an array this fixture does not expect there and is counted as
     * an unmodelled token rather than quietly written. */
    if (tf.ctx_cur > 0) {
        if (cap == GL_VERTEX_ARRAY)
            tf.cli_v = (GLboolean) (on ? GL_TRUE : GL_FALSE);
        else
            tf.v_client_state_unknown++;
    } else if (p == NULL) {
        tf.v_client_state_unknown++;
    } else {
        /* THE POSITION ARRAY IS THE ONE THIS CONTROL MUST NEVER MOVE, so the
         * stub watches it here instead of leaving it to a case to remember. A
         * test that installs state does so through sl_bgtfgl_test_client, which
         * is not a GL call and does not pass through this. */
        if (cap == GL_VERTEX_ARRAY) tf.v_vertex_array_touched++;
        *p = (GLboolean) (on ? GL_TRUE : GL_FALSE);
    }
    r = rec(TFGL_CLIENTSTATE);
    if (r != NULL) {
        r->a = (long) cap;
        r->b = on ? 1L : 0L;
        r->c = (long) tf.client_unit;
    }
}

void glEnableClientState(GLenum cap)
{
    enter();
#ifdef BGTF_MUT_PI_NORESTORE
    /* ONE ARRAY THAT NEVER COMES BACK. The enable that would restore the colour
     * array is swallowed, so the state after the draw differs from the state
     * before it in exactly one field - while the call still counts, which is
     * the point: only the FIELD-FOR-FIELD comparison catches this, and a check
     * that compared the number of disables with the number of enables would
     * pass. */
    if (cap == GL_COLOR_ARRAY) {
        tf.client_state++;
        tf.client_enable++;
        return;
    }
#endif
#ifdef BGTF_MUT_PI_BLINDENABLE
    /* THE RESTORE THAT ENABLES THE WHOLE SET rather than the set that was
     * found: every array this control knows about comes back on, whatever the
     * save saw. The state comparison and the arrays_back count must both fail. */
    cli_set(GL_COLOR_ARRAY, 1);
    cli_set(GL_NORMAL_ARRAY, 1);
    cli_set(GL_FOG_COORD_ARRAY, 1);
#endif
    cli_set(cap, 1);
}

void glDisableClientState(GLenum cap)
{
    enter();
    cli_set(cap, 0);
}

void glGetIntegerv(GLenum pname, GLint *params)
{
    struct sl_bgtfgl_call *r;
    long v = 0L;
    int  nowrite = tf.getintegerv_fail;
    enter();
    tf.get_integerv++;
    if (pname == GL_MAX_TEXTURE_UNITS)
        v = tf.units_reported > 0 ? tf.units_reported : (long) BGTFGL_UNITS;
    else if (pname == GL_MAX_TEXTURE_COORDS) {
        v = tf.coords_reported > 0 ? tf.coords_reported : (long) BGTFGL_UNITS;
#ifdef BGTF_MUT_PI_UNITSDOMAIN
        /* THE DOMAIN REGRESSION. The coordinate-set query answers the
         * CONVENTIONAL UNIT COUNT instead, which is what a sweep bounded by
         * GL_MAX_TEXTURE_UNITS would have covered. On a context where that
         * count is the smaller number, every coordinate array above it stays
         * enabled and feeds the real prepass draw - so the case that installs a
         * coordinate array on a set past the unit count must fail here. */
        v = tf.units_reported > 0 ? tf.units_reported : (long) BGTFGL_UNITS;
#endif
    } else if (pname == GL_CLIENT_ACTIVE_TEXTURE) {
        /* THE ANSWER CAN BE FORCED, so an answer this context could not legally
         * give is put in front of the module WITHOUT moving the modelled
         * selector - which is what lets a case require the context to be
         * unchanged after a refusal. */
        v = tf.client_unit_forced ? tf.client_unit_answer : (long) tf.client_unit;
        if (tf.client_unit_fail) nowrite = 1;
#ifdef BGTF_MUT_PI_SELSANE
        /* THE VALIDATION'S INJECTION, REPAIRED. An out-of-domain or illegal
         * answer is quietly replaced by a legal one before the module sees it,
         * so a module that never validated the saved selector would behave
         * exactly as one that did. The invalid-answer cases, which require a
         * refusal and an untouched context, must fail. */
        v = (long) tf.client_unit;
        nowrite = 0;
#endif
#ifdef BGTF_MUT_PI_SAVEPLAUSIBLE
        /* THE SENTINEL'S OTHER HALF. A query that writes NOTHING instead writes
         * the modelled selector - a perfectly plausible value. The module can
         * only tell a non-answer from an answer because it seeds the save with
         * something no answer can be, so the no-answer case's required refusal
         * must fail here. A save left at 0 would be the same defect: it would
         * read as an answer the context never gave. */
        if (nowrite) { v = (long) tf.client_unit; nowrite = 0; }
#endif
    } else
        tf.v_unknown_enum++;
    r = rec(TFGL_GETINTEGERV);
    if (r != NULL) { r->a = (long) pname; r->b = v; }
    /* A QUERY THAT WRITES NOTHING, which is a real failure's shape: the caller
     * keeps whatever it initialised. */
    if (nowrite || params == NULL) return;
    *params = (GLint) v;
}

void glEnable(GLenum cap)
{
    struct sl_bgtfgl_call *r;
    enter();
    tf.enables++;
#ifdef BGTF_MUT_NODISCARD
    if (cap == GL_RASTERIZER_DISCARD) { /* the enable does nothing */ }
#else
    if (cap == GL_RASTERIZER_DISCARD) tf.discard = 1;
#endif
    else tf.v_unknown_enum++;
    r = rec(TFGL_ENABLE);
    if (r != NULL) r->a = (long) cap;
}

void glDisable(GLenum cap)
{
    struct sl_bgtfgl_call *r;
    enter();
    tf.disables++;
    if (cap == GL_RASTERIZER_DISCARD) tf.discard = 0;
    else tf.v_unknown_enum++;
    r = rec(TFGL_DISABLE);
    if (r != NULL) r->a = (long) cap;
}

void glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr)
{
    struct sl_bgtfgl_call *r;
    enter();
    tf.vertex_pointer++;
    /* AND ON THE AUXILIARY CONTEXT IT MUST LAND IN AN ARRAY OBJECT THIS MODE
     * BOUND (SL_BG_TF_AUX_REAL). A pointer command writes array-object state, so
     * one issued there against the DEFAULT object would be writing state the
     * mode has not accounted for - which is exactly the fall back it refuses the
     * served draw rather than take. */
    if (tf.ctx_cur > 0 && tf.vao_bound == 0u) tf.v_vptr_without_vao++;
    /* A CLIENT POINTER COMMAND CAPTURES THE ARRAY BUFFER BOUND NOW. That is
     * why the module brackets this call with a bind and an unbind, and why a
     * buffer left bound afterwards would turn every later client pointer into
     * an offset into it. */
    tf.vptr.set = 1;
#ifdef BGTF_MUT_VPSIZE
    size = 3;                      /* a model position, not a clip position */
#endif
    tf.vptr.size = size;
    tf.vptr.type = type;
    tf.vptr.stride = stride;
    tf.vptr.off = (long) (size_t) ptr;
    tf.vptr.buf = tf.arr_buf;
    r = rec(TFGL_VERTEXPOINTER);
    if (r != NULL) {
        r->a = (long) size;
        r->b = (long) type;
        r->c = (long) stride;
        r->d = (long) tf.arr_buf;
        r->off = tf.vptr.off;
    }
}

void glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    struct sl_bgtfgl_call *r;
    enter();
#ifndef BGTF_MUT_SONODRAW
    tf.draw_arrays++;
    if (count > 0) tf.draw_vertices += (long) count;
    /* AND WHICH CONTEXT THE DRAW LANDED ON, kept separately from the totals
     * because the auxiliary-context control's whole claim is about placement:
     * "one draw was issued, and it was issued THERE" needs both numbers, and a
     * total alone would pass for an arm that drew on the main context. The
     * transcript record below carries the same stamp per call; this is the
     * summary a signature line can print. */
    if (tf.ctx_cur > 0) {
        tf.draws_on_aux++;
        if (count > 0) tf.draw_vertices_on_aux += (long) count;
    }
#endif
    /* THE PREPASS DRAW'S OWN PRECONDITIONS, as the module states them: no
     * fragment may be produced, the recording buffer must be bound, a program
     * must be current, and the pool must be large enough for one vec4 per
     * vertex - otherwise the draw writes past its end. */
    if (!tf.discard) tf.v_draw_without_discard++;
    if (tf.current == 0u) tf.v_draw_without_program++;
    /* AND A DRAW ON THE AUXILIARY CONTEXT MUST HAVE THE POSITION ARRAY ENABLED
     * THERE (SL_BG_TF_AUX_REAL). An array enable is per-context state held in
     * the bound array object, so a mode that assumed the main context's enable
     * transferred would submit vertices and read nothing - and no count of calls
     * would say so. */
    if (tf.ctx_cur > 0 && !tf.cli_v) tf.v_draw_without_position++;
    if (tf.tf_active) {
        if (tf.tf_buf == 0u) tf.v_draw_without_tf_buffer++;
        else {
            const long have = sl_bgtfgl_buffer_bytes(tf.tf_buf);
            const long long need = (long long) count * 4LL * (long long) sizeof(GLfloat);
            if (have < 0 || (long long) have < need) tf.v_tf_overrun++;
        }
    }
    /* THE CLIENT ARRAYS AS THIS DRAW FOUND THEM, before anything can restore
     * them. This is the one place from which "the draw dereferenced positions
     * only" is a measurement rather than an inference. */
    {
        int u;
        tf.cli_at_draw_seen = 1;
        tf.cli_at_draw_unit = tf.client_unit;
        for (u = 0; u < BGTFGL_UNITCAP; u++) tf.cli_at_draw_t[u] = tf.cli_t[u];
        tf.cli_at_draw_v = tf.cli_v; tf.cli_at_draw_c = tf.cli_c;
        tf.cli_at_draw_n = tf.cli_n; tf.cli_at_draw_i = tf.cli_i;
        tf.cli_at_draw_e = tf.cli_e; tf.cli_at_draw_s = tf.cli_s;
        tf.cli_at_draw_f = tf.cli_f;
    }
    /* THE THREE EMPTY-DRAW CONDITIONS AS THIS DRAW FOUND THEM, before anything
     * can restore them. Every arm of SL_BG_TF_EMPTY_STATE puts the common
     * bracket's state back immediately after the call, so this is the only point
     * at which the rows are distinguishable at all - read after the call they are
     * identical. The per-run totals beside the sample are what make "EVERY draw
     * agreed" a measurement rather than an inference from the last one. */
    tf.st_at_draw_seen++;
    tf.st_at_draw_tf_active = tf.tf_active;
    tf.st_at_draw_discard = tf.discard;
    tf.st_at_draw_program = tf.current;
    if (tf.tf_active)    tf.st_draws_tf_active++;
    if (tf.discard)      tf.st_draws_discard_on++;
    if (tf.current != 0u) tf.st_draws_program_nonzero++;
    r = rec(TFGL_DRAWARRAYS);
    if (r != NULL) {
        r->a = (long) mode;
#ifdef BGTF_MUT_DRAWRANGE
        r->b = (long) first + 1L;  /* the range no longer starts at vertex 0 */
#else
        r->b = (long) first;
#endif
        r->c = (long) count;
        r->d = (long) tf.tf_buf;
    }
}

/* ---- the resolver ------------------------------------------------------- */

/* Every name the module asks for is here except the one a test withholds;
 * nothing else resolves, so a module that reached for a sync point, a
 * read-back, a query or a delete would get NULL and refuse its own capability. */
/* THE GL 1.3 PER-UNIT SELECTOR, handed back only when a test asks for it. Its
 * own name is tfs_*, so the link-time absence of every read-back, fence, sync
 * and query entry point is untouched by its presence. */
static void APIENTRY tfs_ClientActiveTexture(GLenum unit)
{
    struct sl_bgtfgl_call *r;
    enter();
    tf.client_active_texture++;
#ifdef BGTF_MUT_PI_WRONGUNIT
    /* THE SELECTOR THAT LANDS ONE UNIT AWAY. Every per-unit read and write goes
     * to the wrong array, so a save of unit 1 restores unit 2 - which is what a
     * control that got its unit arithmetic wrong would do, and which the state
     * comparison must catch. */
    unit = (GLenum) (unit + 1u);
#endif
    tf.client_unit = (GLint) unit;
    (void) cli_unit_index();            /* range-check it, and count a miss */
    r = rec(TFGL_CLIENTACTIVETEX);
    if (r != NULL) r->a = (long) unit;
}

/* ---- THE BACKEND'S AUXILIARY GL CONTEXT, FAKED ---------------------------
 *
 * The product's src/gfx/sl_gfx_sdl.c supplies these four for real; this file
 * supplies them here for the same reason it supplies sl_gl_proc - so the module
 * is the only thing under test and a context switch becomes an observable event
 * rather than a platform call nobody can see. Each one keeps the contract the
 * module's header states, including which context is left current on each
 * failure, because that is precisely what the control's fail-closed paths turn
 * on.
 *
 * A HELPER IS NOT A GL CALL. None of these goes through `enter`: they are
 * platform calls, not entry points, so they do not raise `tf.calls` and they
 * are not stamped into the transcript. What they move is `tf.ctx_cur`, and every
 * GL call after them is attributed by it.
 *
 * THE FAILURE-AFTER COUNTERS ARE WHAT MAKES THE SECOND SWITCH TESTABLE. The
 * acquire performs one complete switch round trip of its own, so a gate that
 * bit on the first call could never reach the draw; a case that wants the draw's
 * switch to fail lets the acquire's succeed. -1 never, 0 the first call. */
static int gate_bites(int on, long *after)
{
    if (!on || *after < 0L) return 0;
    if (*after > 0L) { (*after)--; return 0; }
    return 1;
}

int sl_gl_aux_create(void);
int sl_gl_aux_create(void)
{
    tf.aux_create_calls++;
    if (tf.aux_exists) return 1;
    if (gate_bites(tf.aux_create_fail, &tf.aux_create_fail_after)) {
        /* NOTHING CREATED AND THE MAIN CONTEXT UNTOUCHED, which is what a
         * create failure means to a caller. */
        return 0;
    }
    tf.aux_exists = 1;
    tf.aux_created++;
    if (gate_bites(tf.aux_create_lost, &tf.main_switch_fail_after)) {
        /* CREATED, AND THE SWITCH BACK FAILED. The current context is modelled
         * as NONE, exactly as it is for a failed sl_gl_main_make_current below,
         * and the reason is the same: what a failed switch leaves current is not
         * something the platform promises, so the honest model is the one the
         * module's own reasoning uses - UNKNOWN. -1 is that, and it is what makes
         * "the control issues nothing once the main context is lost" a
         * measurement: any call at all raises v_call_no_context. Modelling it as
         * the auxiliary context instead would make a stray call land somewhere
         * plausible and go unnoticed. */
        tf.ctx_cur = -1;
        return -1;
    }
    /* THE CREATE MADE IT CURRENT AND THE BACKEND SWITCHED BACK, so a caller
     * returning from here is on the MAIN context. */
    tf.ctx_cur = 0;
    return 1;
}

int sl_gl_aux_make_current(void);
int sl_gl_aux_make_current(void)
{
    tf.aux_switch_calls++;
    if (!tf.aux_exists) return 0;
    /* THE UNKNOWN OUTCOME IS ASKED FIRST, because it is the stronger failure: a
     * case arming both means to drive the one that cannot be recovered from. It
     * leaves NO context current, which is the honest model of a switch after
     * which the backend's own query named neither context - and it is what makes
     * every later GL call a v_call_no_context violation. */
    if (gate_bites(tf.aux_switch_unknown, &tf.aux_switch_unknown_after)) {
        tf.ctx_cur = -1;
        return -1;
    }
    if (gate_bites(tf.aux_switch_fail, &tf.aux_switch_fail_after)) {
        /* THE MAIN CONTEXT IS VERIFIED CURRENT - that is what this failure
         * means, and why the control can let the ordinary restoration run after
         * it. */
        return 0;
    }
    tf.ctx_cur = 1;
    tf.aux_switches++;
    return 1;
}

int sl_gl_main_make_current(void);
int sl_gl_main_make_current(void)
{
    tf.main_switch_calls++;
    if (gate_bites(tf.main_switch_fail, &tf.main_switch_fail_after)) {
        /* NO CONTEXT IS CURRENT. This is the strict model on purpose: a switch
         * that failed established nothing, so every GL call after it raises
         * v_call_no_context - which turns "the control issues nothing once the
         * main context is lost" into a measurement rather than a reading of the
         * source.
         *
         * AND -1 IS THE BACKEND'S ONLY FAILURE ANSWER HERE, by that function's
         * contract: it is called only while the auxiliary context is current, so
         * there is no recoverable spelling of its failure to model. The caller
         * reads any answer other than 1 as the unknown outcome, so this value is
         * what the backend returns rather than what the module depends on. */
        tf.ctx_cur = -1;
        return -1;
    }
    tf.ctx_cur = 0;
    tf.main_switches++;
    return 1;
}

int sl_gl_aux_destroy(void);
int sl_gl_aux_destroy(void)
{
    if (!tf.aux_exists) return 0;
    /* THE BACKEND VERIFIES ITS OWN PRECONDITION NOW, so this models the answer
     * it gives when the main context is not verifiably current: nothing is
     * destroyed, the context stays alive, and the caller is told the current
     * context is unknown. A case arms this to drive the teardown that must NOT
     * then go on to delete the main context and present a window. */
    if (tf.aux_destroy_unknown) {
        tf.aux_destroy_refused++;
        return -1;
    }
    tf.aux_exists = 0;
    tf.aux_destroyed++;
    return 1;
}

/* THE TERMINATE SEAM, INTERCEPTED SO THAT IT CAN BE ASSERTED.
 *
 * The product's sl_gl_terminate_no_gl ends the process without running exit
 * handlers, which is what makes "no GL call follows a lost context" complete
 * rather than hopeful. A fixture that let it do that would assert nothing, so
 * this records the call and RETURNS - and every entry point's funnel counts the
 * calls made after it, which is the property under test. */
void sl_gl_terminate_no_gl(int code);
void sl_gl_terminate_no_gl(int code)
{
    tf.fatal_calls++;
    tf.fatal_code = code;
}

void *sl_gl_proc(const char *name)
{
    static const struct { const char *name; void *fn; } table[BGTFGL_PROCS] = {
        { "glCreateShader",              (void *) tfs_CreateShader },
        { "glShaderSource",              (void *) tfs_ShaderSource },
        { "glCompileShader",             (void *) tfs_CompileShader },
        { "glGetShaderiv",               (void *) tfs_GetShaderiv },
        { "glGetShaderInfoLog",          (void *) tfs_GetShaderInfoLog },
        { "glCreateProgram",             (void *) tfs_CreateProgram },
        { "glAttachShader",              (void *) tfs_AttachShader },
        { "glLinkProgram",               (void *) tfs_LinkProgram },
        { "glGetProgramiv",              (void *) tfs_GetProgramiv },
        { "glGetProgramInfoLog",         (void *) tfs_GetProgramInfoLog },
        { "glUseProgram",                (void *) tfs_UseProgram },
        { "glGetUniformLocation",        (void *) tfs_GetUniformLocation },
        { "glUniform1f",                 (void *) tfs_Uniform1f },
        { "glUniform3f",                 (void *) tfs_Uniform3f },
        { "glUniform1i",                 (void *) tfs_Uniform1i },
        { "glUniform1fv",                (void *) tfs_Uniform1fv },
        { "glTransformFeedbackVaryings", (void *) tfs_TransformFeedbackVaryings },
        { "glBindBufferBase",            (void *) tfs_BindBufferBase },
        { "glBeginTransformFeedback",    (void *) tfs_BeginTransformFeedback },
        { "glEndTransformFeedback",      (void *) tfs_EndTransformFeedback },
        { "glGenBuffers",                (void *) tfs_GenBuffers },
        { "glBindBuffer",                (void *) tfs_BindBuffer },
        { "glBufferData",                (void *) tfs_BufferData },
        { "glGetBufferParameteriv",      (void *) tfs_GetBufferParameteriv }
    };
    size_t i;
    /* A resolve is not a GL call, so it does not raise tf.calls - but it is
     * still refused after teardown, and still counted, because "no entry point
     * is resolved while the switch is off" is one of the properties. */
    tf.proc_calls++;
    if (tf.context_destroyed) tf.v_call_after_context_destroyed++;
    if (name == NULL) return NULL;
    if (tf.nasked < (int) (sizeof tf.asked / sizeof tf.asked[0]))
        tf.asked[tf.nasked++] = name;
    /* WHICH CONTEXT THE RESOLVE WAS MADE ON, which is the whole question the
     * per-context pointer pair exists to answer: a pointer is only valid on the
     * context it was resolved on, so "it was resolved THERE" has to be
     * observable and not inferred from the pointer being used there. */
    if (tf.ctx_cur > 0) tf.proc_calls_on_aux++;
    /* AND ONE NAME MAY BE WITHHELD ON THE AUXILIARY CONTEXT ALONE. That is the
     * shape of a context that cannot supply its own entry point, and the one
     * case in which the module must refuse rather than reach for the main
     * context's pointer - a fall back no count would notice. */
    if (tf.ctx_cur > 0 && tf.withhold_proc_on_aux != NULL
        && strcmp(name, tf.withhold_proc_on_aux) == 0)
        return NULL;
    /* AND THE FENCE PATH'S THREE NAMES MAY BE WITHHELD ON EITHER CONTEXT
     * INDEPENDENTLY (SL_BG_TF_AUX_SYNC). Two fields rather than one because the
     * module resolves its MAIN set and its AUXILIARY set at different points in
     * the acquire and refuses with different lines, so the two have to be
     * failable apart - and because "this context cannot supply the set" is the
     * one case in which the module must refuse rather than reach for the other
     * context's pointer, a fall back no count would notice. */
    if (tf.ctx_cur == 0 && tf.withhold_fence_main != NULL
        && strcmp(name, tf.withhold_fence_main) == 0)
        return NULL;
    if (tf.ctx_cur > 0 && tf.withhold_fence_aux != NULL
        && strcmp(name, tf.withhold_fence_aux) == 0)
        return NULL;
    if (tf.withhold_proc != NULL && strcmp(name, tf.withhold_proc) == 0) return NULL;
    /* THE PER-CONTEXT PAIRS. A resolve made while the auxiliary context is
     * current answers with THAT context's pointer; the same name resolved on the
     * main context answers with the main one's. Both do the same work, and the
     * difference is only which context each is valid on - see the note above
     * tfs_IsProgram. glUseProgram is intercepted here, above the table below,
     * for exactly that reason: the table holds the main context's. */
    if (tf.ctx_cur > 0 && strcmp(name, "glUseProgram") == 0)
        return (void *) tfs_UseProgram_aux;
    if (tf.ctx_cur > 0 && tf.provide_is_program
        && strcmp(name, "glIsProgram") == 0)
        return (void *) tfs_IsProgram_aux;
    /* THE REAL-PREPASS MODE'S PAIRS (SL_BG_TF_AUX_REAL). Eleven names, each
     * resolving to THAT context's pointer while the auxiliary context is
     * current, on exactly the terms the two above do. Three of them -
     * glIsBuffer and the array-object pair - exist only for this mode and are
     * gated on `provide_real_set`; the other eight are names the main path
     * already resolves, and here they answer with the auxiliary twin so that a
     * module calling a main-context address on that context is caught. */
    if (tf.ctx_cur > 0) {
        if (strcmp(name, "glBindBuffer") == 0)
            return (void *) tfs_BindBuffer_aux;
        if (strcmp(name, "glBindBufferBase") == 0)
            return (void *) tfs_BindBufferBase_aux;
        if (strcmp(name, "glBeginTransformFeedback") == 0)
            return (void *) tfs_BeginTransformFeedback_aux;
        if (strcmp(name, "glEndTransformFeedback") == 0)
            return (void *) tfs_EndTransformFeedback_aux;
        if (strcmp(name, "glUniform1f") == 0)
            return (void *) tfs_Uniform1f_aux;
        if (strcmp(name, "glUniform3f") == 0)
            return (void *) tfs_Uniform3f_aux;
        if (strcmp(name, "glUniform1i") == 0)
            return (void *) tfs_Uniform1i_aux;
        if (strcmp(name, "glUniform1fv") == 0)
            return (void *) tfs_Uniform1fv_aux;
        if (tf.provide_real_set) {
            if (strcmp(name, "glIsBuffer") == 0)
                return (void *) tfs_IsBuffer_aux;
            if (strcmp(name, "glGenVertexArrays") == 0)
                return (void *) tfs_GenVertexArrays_aux;
            if (strcmp(name, "glBindVertexArray") == 0)
                return (void *) tfs_BindVertexArray_aux;
        }
        /* THE FENCE PATH'S AUXILIARY TRIO (SL_BG_TF_AUX_SYNC), on its own gate.
         * NOT in the twenty-four-name table, so the ordinary fixture's "exactly
         * these twenty-four, in this order" and the withhold-each-one battery are
         * untouched and a resolve answers NULL by default - which is what makes
         * "a FINISH launch resolves no fence entry point" observable from this
         * side rather than asserted from the module's. */
        if (tf.provide_fence_set) {
            if (strcmp(name, "glFenceSync") == 0)
                return (void *) tfs_FenceSync_aux;
            if (strcmp(name, "glWaitSync") == 0)
                return (void *) tfs_WaitSync_aux;
            if (strcmp(name, "glDeleteSync") == 0)
                return (void *) tfs_DeleteSync_aux;
        }
    }
    /* AND THE MAIN CONTEXT'S OWN TRIO, on the same gate and with the same
     * reasoning: the main-context addresses exist so that a module reaching for
     * one on the auxiliary context is caught by the identity guard rather than by
     * a NULL. */
    if (tf.provide_fence_set) {
        if (strcmp(name, "glFenceSync") == 0)
            return (void *) tfs_FenceSync;
        if (strcmp(name, "glWaitSync") == 0)
            return (void *) tfs_WaitSync;
        if (strcmp(name, "glDeleteSync") == 0)
            return (void *) tfs_DeleteSync;
    }
    /* AND THE MAIN CONTEXT'S OWN THREE, on the same gate: NOT in the
     * twenty-four-name table, so the ordinary fixture's "exactly these
     * twenty-four, in this order" and the withhold-each-one battery are
     * untouched, and a resolve answers NULL by default. The main-context
     * addresses exist so that a module reaching for one on the auxiliary context
     * is caught by the identity guard rather than by a NULL. */
    if (tf.provide_real_set) {
        if (strcmp(name, "glIsBuffer") == 0)
            return (void *) tfs_IsBuffer;
        if (strcmp(name, "glGenVertexArrays") == 0)
            return (void *) tfs_GenVertexArrays;
        if (strcmp(name, "glBindVertexArray") == 0)
            return (void *) tfs_BindVertexArray;
    }
    /* THE DIAGNOSTIC'S ONE, AND ONLY WHEN A TEST ASKS. It is NOT in the table
     * above, so the ordinary fixture's "exactly these twenty-four, in this
     * order" is untouched and a resolve of it answers NULL - which is both the
     * default and the shape of a driver without GL 1.5 buffer reads.
     *
     * glGetError IS DELIBERATELY NOT RESOLVABLE HERE. The module calls the core
     * export directly, so a resolver entry for it would be dead and would
     * quietly permit a regression back to the extension-loader form this stub
     * exists to rule out: a module that resolved it would get NULL and record
     * an unobservable error state. */
    if (tf.provide_readback && strcmp(name, "glGetBufferSubData") == 0)
        return (void *) tfs_GetBufferSubData;
    /* THE NEUTRAL-PROGRAM CONTROL'S ONE, on the same terms and for the same
     * reasons: not in the table above, so the ordinary fixture's "exactly these
     * twenty-four, in this order" and the withhold-each-one battery are
     * untouched, and a resolve answers NULL by default - which is the shape of a
     * context that will not hand back a delete and the case the control must
     * REFUSE on rather than creating an object it could never release. */
    if (tf.provide_delete_program && strcmp(name, "glDeleteProgram") == 0)
        return (void *) tfs_DeleteProgram;
    /* AND ITS SECOND, GATED SEPARATELY so a case can withhold either release
     * alone and see the control refuse on each. */
    if (tf.provide_delete_shader && strcmp(name, "glDeleteShader") == 0)
        return (void *) tfs_DeleteShader;
    /* THE AUXILIARY-CONTEXT CONTROL'S ONE, on the same terms: not in the table
     * below, so the ordinary fixture's "exactly these twenty-four, in this
     * order" and the withhold-each-one battery are untouched, and a resolve
     * answers NULL by default - which is the shape of a context that cannot be
     * asked whether it shares names and the case that control must REFUSE on
     * rather than creating a context it could never verify. */
    if (tf.provide_is_program && strcmp(name, "glIsProgram") == 0)
        return (void *) tfs_IsProgram;
    /* THE POSITION-ONLY-INPUT CONTROL'S ONE, on the same terms and for the same
     * reasons: not in the table above, so the ordinary fixture's "exactly these
     * twenty-four, in this order" is untouched, and a resolve answers NULL by
     * default - which is the shape of a context without the GL 1.3 per-unit
     * selector and the case the module must refuse to isolate on. */
    if (tf.provide_client_active_texture
        && strcmp(name, "glClientActiveTexture") == 0)
        return (void *) tfs_ClientActiveTexture;
    for (i = 0; i < sizeof table / sizeof table[0]; i++)
        if (strcmp(name, table[i].name) == 0) return table[i].fn;
    return NULL;
}

/* ---- THE GL-STATE PROBE'S PHASE HOOKS, INERT (SL_BG_GLSTATE) --------------
 *
 * WHY THEY ARE HERE. The module under test calls three of src/gfx/sl_gfx_bgstate.c's
 * hooks UNCONDITIONALLY - the call is guarded by sl_bgstate_armed() answering
 * true, but the reference to the symbol is not - so this fixture cannot link
 * without them. They are supplied here rather than by adding that translation
 * unit to the link because it takes the REAL GL headers and issues real GL
 * commands, and this fixture replaces GL entirely: linking it would put a
 * second, unsynthesised driver behind the module under test.
 *
 * INERT IS THE OFF LAUNCH'S OWN ANSWER, not a convenience. The probe is a
 * diagnostic that arms only when SL_BG_GLSTATE is set; no run of this fixture
 * sets it, and the auxiliary real-prepass mode refuses outright when it is set,
 * so an unarmed answer is what every scenario here would get from the real
 * module. What the hooks do when ARMED is covered where the real translation
 * unit is linked, in tools/profiling/native/test_sl_gfx_bgtf_seam.c.
 *
 * THE PARAMETER TYPE IS LEFT INCOMPLETE on purpose: these take a pointer to a
 * struct this fixture has no business constructing, and an incomplete type both
 * matches the real declarations' ABI and makes it impossible for a stub to read
 * through the pointer. */
struct sl_bgstate_producer;

int sl_bgstate_armed(void)
{
    return 0;
}

void sl_bgstate_pre_setup(const struct sl_bgstate_producer *p)
{
    (void) p;
}

void sl_bgstate_post_draw_raw(const struct sl_bgstate_producer *p)
{
    (void) p;
}

void sl_bgstate_post_restore(const struct sl_bgstate_producer *p)
{
    (void) p;
}

void sl_bgstate_reuse(const struct sl_bgstate_producer *p)
{
    (void) p;
}
