# Native boot design (T3 core, next implementation chunk)

Status: DESIGN, mapped 2026-08-22 against the real code. The skeleton links
and runs; this is the plan for making the sim TICK.

## The boot chain, measured

`mainproc` (src/init.c) -> idleCreateThread, piCreateManager,
rmonCreateThread, tokenReadIo, schedulerInitThread -> `bossEntry` (src/boss.c).

`bossEntry` is an infinite loop that blocks on `gfxFrameMsgQ` - frame
messages the scheduler pumps once per VI retrace - and submits SP/DP tasks
for rendering. Everything downstream of boot is message-driven off that
queue. The scheduler (src/sched.c) is the metronome; the game is the dancer.

## Native model: the recv-pump inversion

Rather than porting threads, invert the metronome: implement `osRecvMesg` so
that blocking on an EMPTY frame queue IS the frame boundary. When the game
blocks awaiting the next frame message: advance fixed-step time, deliver the
retrace message, return. The game believes a scheduler exists; the scheduler
is the act of waiting. Single-threaded, no ucontext, no pthreads - the
sm64/PD lineage proved the shape, ours is simpler because the skeleton is
headless first.

Thread functions in the shim become bookkeeping: osCreateThread records the
entry point, osStartThread runs nothing (idle/rmon threads are irrelevant
natively; the "main" thread IS the process). osSpTaskStartGo marks the task
complete immediately in headless mode - later it hands the DL pointer to the
oracle, which is the T2 tap point, already identified.

## Determinism from the first tick

osGetCount returns `base + ticks * CYCLES_PER_FRAME` - fixed step, no wall
clock, exactly what Phase 0 proved the sim needs. The recorder's input
streams feed osContGetReadData. EEPROM reads come from the same cartridge
save files the trace corpus carries. Consequence: the FIRST fully-booted
native tick is trace-comparable against the emulator corpus with no
additional work - `trace-verify` gains a `--backend native` and the state
reader points at the process's own memory instead of RDRAM.

The stride lesson applies with force: native struct layout (gcc -m32 +
-fms-extensions) need not match the N64's. The state reader for the native
backend reads FIELDS via a generated offset table from the native build, not
the N64 offsets. Generate it, never assume it.

## What blocks the first tick (ordered)

1. PI DMA (osPiStartDma/osPiRawStartDma/osPiReadIo) -> reads from the ROM
   file. This is romdata territory and MUST route through the existing
   verify-rom gate; the ROM path comes from the same place the harness gets
   it.
2. The recv-pump shim (message queues grow receive + the pump).
3. Asset segment loads land in real memory (mema/memp already compile).
4. tokenReadIo: return the empty token path (rmonGetToken -> no tokens) so
   boot takes the retail branch - direct-boot levels come from the same
   SL_DIRECT_BOOT_LEVEL define, which already works.
5. Audio task submission: complete immediately, discard.

Milestone criterion: bossEntry reaches its main loop and survives 1000
pumped frames headless; then g_StageNum/g_SelectedDifficulty read back
correctly via the native offset table; THEN wire trace-verify.


## T4's true shape (discovered 2026-08-22, post-boot)

The byte-order wall turned out to be half of a two-layer problem, and the
other half is remarkable: GoldenEye DEMAND-PAGES THE CARTRIDGE. Asset blobs
bake pointers into a TLB-mapped virtual window (0xC0xxxxxx); touching an
unmapped page faults; the exception handler (crash.c:365 ->
tlbmanageTranslateLoadRomFromTlbAddress) decodes the faulting address into a
ROM offset (mask 0x7FFFE000, 8KB pages) and DMA-loads that page into a TLB
block. Rare built virtual memory over a cartridge. Anim headers confirm it:
frame counts read correctly as big-endian, and bitDescriptors/bitStream hold
0xC034xxxx self-references.

Native mirror, layer 1 - the pager: reserve the same virtual window with
mmap PROT_NONE (a 32-bit process on a 64-bit kernel owns the full 4GB, so
MAP_FIXED at 0xC0000000 is available); the existing SIGSEGV handler in
sl_main becomes the pager - fault in window -> decode ROM offset exactly as
tlbmanage does -> read the 8KB from the ROM file -> mprotect READ. The
game's own addressing then works untouched, and tlbmanage's bookkeeping can
be bypassed natively (the guarded heap site already sidesteps its block
arithmetic).

Layer 2 - representation: paged-in bytes are still big-endian, and readers
access fields through LE structs. The bit-stream payloads are byte-oriented
and endian-neutral (modelAnimReadRootMotionValue walks bytes); it is the
HEADERS - u16/u32 fields and those baked pointers - that need swapping, and
swapping is per-format knowledge. This is the irreducible T4 transcode
core, now scoped tightly: headers only, starting with ModelAnimation and
the model file headers, exactly the structs the decomp already defines.

Order of work: pager first (unblocks addressing globally, one mechanism),
then header swaps per format as each is touched, verified by the loader
progressing further - with the frame-count sanity (0x0051 = 81 frames) as
the per-format smoke test.


## The title screen lives (2026-08-22, second stretch)

Two mechanisms and one convention took the skeleton from "3 frames per
2000 pumps" to the title stage running with a perfect frame rhythm.

**Scheduler task replies.** The frame stall was pendingGfx: boss submits
gfx tasks via rspGfxTaskStart -> osSendMesg(sched_cmdQ, sctask), and the
work gate is `pendingGfx < 2`. The scheduler thread that would reply never
runs natively, so pendingGfx hit 2 and froze. The shim intercepts sends to
sched_cmdQ and delivers each task's own reply message (sctask->msg, the
game's {OS_SC_DONE_MSG}) to sctask->msgQ on the next frame pump. Result:
DONE/RETRACE alternate, exactly 2 pumps per frame, indefinitely.

**The endian convention (the T4 keystone).** Dynamic DLs are built by gbi
macros writing native words - that is unchangeable without rewriting gbi.h.
Therefore: EVERYTHING word-shaped is native order in memory. File data
gets swapped at its load choke point, once:

- model files: nodes + per-opcode rodata inside the promote walker
  (visits each node exactly once); DLs via sl_gfx_dl_swap (walk to
  G_ENDDL, follow G_DL) after promote, before the tex parsers run
- fonts: whole struct font is uniform words - sl_swap_words after romCopy
- language banks: u32 offset table at head, extent = first string offset
- global image table (texReset): uniform 8-byte Gfx records, wholesale
- RLE image headers: BE u16 w/h reads

Byte-positional DL parsers (bytes[0] == opcode) read SL_DLOP - the top
byte of w0 - natively; sl_endian.h holds the accessors and the rule.
Bitfield structs compiled from C are native and self-consistent, but any
raw-word access that assumes BE bit packing (texLoad's 0xFFFFFF mask over
g_Textures) must use the field access natively.

Milestone state at 20000 pumps: g_StageNum = 90 (title stage loaded and
handed off), currentFrameCounter = 10000, gunbarrel eye intro rendering,
zero crashes across repeated runs. The boot criterion from this doc is
met and exceeded: the game plays its attract sequence headless.


## The attract loop cycles (2026-08-22, third stretch)

120,000 pumps, zero faults: the native build runs GoldenEye's full
attract sequence indefinitely - title, logos, cast roll, a random
campaign-level demo with animating guards, stage teardown, and around
again.  60,934 frames rendered across multiple complete cycles.

What this stretch established:

- **Demo (ramrom) playback works natively.**  One structure-padding trap:
  IDO pads ramromfilestructure to 232 bytes (u64 tail alignment), gcc
  gets 228 - the stream stride desynced silently.  Wire-format strides
  must come from the N64's layout, not native sizeof.
- **The N64 memory model is a bug-absorber the native build must
  replace case by case.**  Recurring classes now catalogued: reads
  through garbage pointers (everything readable via the cartridge TLB),
  fixed stack arrays that spill into deliberate padding locals (gcc
  reorders locals; the canary catches what the N64 absorbed), sentinel
  scans that run off arrays until a byte appears, and file records whose
  runtime slots carry garbage nobody zeroes.
- **Anim wire headers tile at 0x30**, not the struct's 0x40 - and swap
  passes must never overlap: the second sweep of a shared byte undoes
  the first.  Once-only is a hard invariant for every T4 pass.
- **Stale objects lie.**  The N64 Makefile has no header dependency
  tracking; a months-old field-binding regression (setupCctv's pad ->
  lookpad) surfaced only when prop.c itself changed.  MATCH gates after
  header edits need clean rebuilds of affected objects.


## Replay parity: the plumbing works (2026-08-22, fourth stretch)

trace-verify --backend native is nearly real.  The native build replays a
recorded run (SL_INPUT + SL_EEPROM + SL_BOOT_* + SL_TICKS) and captures
schema v10 state hashes per frame (SL_TRACE_OUT); nativediff.py aligns
and compares against the emulator corpus per subsystem.

What parity took, beyond the input/eeprom/boot plumbing:

- **Recorded frame timing.**  The emulator's frame cadence is physical
  (a frame takes 2-3 emulated VIs); natively it is replay data,
  extracted from the trace's frame-counter jumps and fed through
  waitForNextFrame's own elapsed formula.  The clock unit is the game's
  divisor (775875), making t units yield exactly t ticks.  Result:
  native frame counters equal the emulator's EXACTLY, value for value.
- **IEEE single semantics** (-msse2 -mfpmath=sse): removed every one-ulp
  x87 drift; positions became bit-identical.
- **The gu du-union trap**: {hi, lo} double word pairs are big-endian
  layout; natively they built garbage trig coefficients and every
  rotation matrix from gu sinf/cosf was junk (glass placement NaN'd).
- Per-VI emulator samples are settled-sim states (samples land during
  rendering); collapse to the last tick of each constant-fc run and they
  pair 1:1 with native frame boundaries.

Score on archives-sweep-agent: player hash byte-identical for 53 live
frames; all 304 props field-identical at the first live frame.  Open:
door frac (setupDoor travel displacement) computes different values -
first suspect for the tick-53 player divergence too.  Schema v11 (weapon
identity instead of raw pointers in chr hashes) is queued for when chr
parity starts mattering.
