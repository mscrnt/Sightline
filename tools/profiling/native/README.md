# Native renderer timing producer and its offline mocks

The producer is `src/gfx/sl_gfx_profile.c` with `src/gfx/sl_gfx_profile.h`. This
directory holds only its offline test fixtures: a synthetic GL/clock/counter
stub and two test programs. Nothing here is compiled into the game, and nothing
here measures a real GPU. For the surrounding policy and the evidence rules, see
[the readiness guide](../READINESS.md).

**Status:** the timestamp path has passed a bounded Facility check on OpenGL
4.6.0 / NVIDIA 591.86. See the [live validation record](../../../docs/decisions/profiling-live-validation.md).
Other driver and fallback paths remain covered by mocks rather than live
qualification. Every number the mock programs print is fabricated.

## Default off

`sl_gfx_profile_active()` reads `SL_RENDER_PROFILE` once per process and caches
the answer, so a default run pays one `getenv` for the process lifetime and a
cached integer test thereafter. The variable's value is the destination CSV path.
The destination is opened `O_CREAT|O_EXCL`: an existing path fails rather than
being appended to or replaced, so no earlier evidence can be overwritten.

With the variable unset, every entry point below returns immediately and no GL
call, allocation or file access happens.

## API

| call | purpose |
| --- | --- |
| `sl_gfx_profile_active()` | is the producer on for this process |
| `sl_gfx_profile_context_invalidate()` | called at both ends of a GL context's life; caches, entry points and query names belong to one context and are never inherited |
| `sl_gfx_profile_scope_begin(name)` / `_scope_end(name)` | a nested span; `name` must match `[a-z0-9_-]+` and be shorter than `SL_GFX_PROFILE_NAME_MAX` |
| `sl_gfx_profile_frame_begin()` | opens the whole-frame scope and runs the one non-blocking availability pass over records already in flight |
| `sl_gfx_profile_frame_end()` | the frame was presented |
| `sl_gfx_profile_frame_end_held()` | the swap was held |
| `sl_gfx_profile_shutdown()` | teardown, called while the GL context is still current |
| `sl_gfx_profile_counts_get(out)` | what the producer did: counts, not measurements |

Scopes are a strict LIFO stack bounded by `SL_GFX_PROFILE_MAX_DEPTH`. A begin
past that bound, a name that fails validation, an unbalanced end and a scope
group that never closes are all counted as misuse and record nothing; none of
them invents a row.

## Frame identity

The outermost scope opens a **scope group**. Every record in the group takes its
frame identity from the pumped-frame counter as it stands when that outermost
scope closes, which is the value the regular present is recorded at. One frame
therefore yields one number across all its scopes, and a nested scope joins its
enclosing frame. No row is written before its identity is known, and a record
whose group never closes produces no row at all.

This holds only while the native pump ends the previous frame, notes the regular
present, begins the next frame, and advances the counter after both. That
ordering is checked offline, as source text, by the frame-pump order guard in
`readiness.py`, and behaviourally by `regress_frame_identity.c` below. Neither
check proves the shipped binary's behaviour.

## The two series

| scope | written when | meaning |
| --- | --- | --- |
| `frame` | the frame was presented | exactly the regular-present grid |
| `frame-held` | the swap was held | work that happened but was not presented |

Keeping these apart is what stops held work from being selected as presented
work by a regular-present frame grid. Any other scope name is its own series and
is reported, never summed into another.

## CSV schema

```text
frame,scope,cpu_submit_ms,gpu_elapsed_ms,gpu_status
```

One row per `(frame, scope)`; a repeat of that pair is dropped rather than
written. `gpu_status` is `ready`, `pending` or `unsupported`, and
`gpu_elapsed_ms` is empty for the latter two - a missing GPU time is never
backfilled with host time or a zero. Rows are flushed as written, so a partial
run keeps what it had produced; a write failure stops further rows instead of
discarding the file.

### What the two times are

`cpu_submit_ms` for the `frame` scope is a **clear-to-present CPU wall span**.
The pacing wait, the game and phase work, the developer probes and the counter
increment all sit inside it. It is not renderer submission cost and not sampled
CPU execution time.

`gpu_elapsed_ms` is the elapsed span between two points on the GPU timeline. It
is **not exclusive GPU-busy time**: unrelated work may occupy the GPU within it.

The two spans overlap, so subtracting one from the other does not isolate a
bottleneck and must not be reported as doing so. A narrower renderer-only figure
needs a narrower scope.

## Bounded asynchronous queries

GPU time comes from asynchronous timer queries, never from a synchronous wait.
There is no `glFinish`, no `glFlush`, no spin, and no `GL_QUERY_RESULT` read
before `GL_QUERY_RESULT_AVAILABLE` reports the result ready. Availability is
polled once per frame, in `frame_begin`, so a result is collected on a later
submission than the one that issued it.

- `SL_GFX_PROFILE_RING` is 64 records in flight, with `2 x 64` query names
  generated once per context. The bound exists so a driver that stops retiring
  queries cannot grow this subsystem's memory or query-object count without
  limit.
- A full ring **drops** the record: no row at all, counted as `dropped_ring`,
  visible downstream as a gap in the frame column rather than as a row with an
  invented number.
- Generated names are adopted and counted before the pool is judged usable, so a
  short or partial pool is reported `unsupported` for timing and still has every
  name this process owns deleted at teardown, while the context is current.
- Two GL paths, selected by capability: `GL_TIMESTAMP` counter pairs, where the
  spec permits timestamp queries inside another query's interval, so nesting is
  timed; and a `GL_TIME_ELAPSED` fallback, where only one timer query may be
  active at a time, so a nested scope there is reported `unsupported` with an
  empty GPU field rather than being given the outer scope's time.
- A GL failure inside a query turns the producer honestly `unsupported` from
  that point on; it does not retry or estimate.

The ring size and depth bound are reasoned from the retirement latency the spec
allows, not measured against a driver.

Primary authority for the query semantics, the timestamp-inside-interval rule
and the single-active-interval restriction:
[ARB_timer_query](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_timer_query.txt).

## Files here

| file | what it is |
| --- | --- |
| `sl_gfx_profile_testgl.h` / `.c` | synthetic GL entry points, clock, environment and frame counter, plus a watchdog; built only under `-DSL_GFX_PROFILE_TEST` |
| `test_sl_gfx_profile.c` | the producer unit suite: 16 cases, 103 checks |
| `regress_frame_identity.c` | the frame-identity regression: 33 checks |

Each test program has its own `main()`, so each is linked into its own
executable. `check.py --native-mocks` compiles both against the producer and the
stub, inspects each binary's DLL imports before running it, and refuses any
import outside `kernel32.dll` and `msvcrt.dll`. The stub declares no blocking GL
symbol, so the absence of `glFinish`, `glFlush` and a blocking result read is
enforced by the link, not only by review.

`regress_frame_identity.c` drives the pump's real ordering with a nested scope,
held frames and an iteration where the counter does not advance, and asserts
that each row's frame equals the counter value at the moment that frame ended,
that a nested scope joins its enclosing frame, and that a held frame never
reaches the regular-present series. Against an earlier producer that read the
identity at frame open, the same regression fails 7 of its 33 checks, so it
discriminates rather than merely passing.

## Remaining qualifications

1. Mocks alone establish no native or performance claim. The separate live
   validation covers one Facility pose and one driver, not the whole suite or
   a supported frame-rate budget.
2. The frame-pump order guard is text inspection, not a proof of C semantics or
   of build provenance.
3. A per-row misattribution inside a correct frame grid is not detectable from
   the CSV. The property is held in the producer by stamping a whole scope group
   from a single counter read, not by a downstream check.
4. An armed screenshot or mark performs readbacks inside the frame span, so a
   profiling run must leave them disarmed. The high-refresh extra-presentation
   path is deliberately unhooked.
5. No thread safety: the producer assumes the single thread that owns the GL
   context.
