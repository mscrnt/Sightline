# Phase 1 tracks — the plan of record

Status: ACTIVE. Owner opened Phase 1 on 2026-08-21 with the directive to
improve on prior art, not copy it. Supersedes the earlier proposal in this
file; the Perfect Dark port research that informed both is summarised at the
bottom.

## Strategy in one paragraph

The PD/sm64 ports interpreted F3DEX display lists because touching game code
safely was unaffordable for hand-verified efforts. Making game-code change
safe is what our Phase 0 built - 97 recordings, per-tick state hashing over
guards, perception, and world props, gating every edit. So we cut one level
higher than they could afford to: replace display-list CONSTRUCTION with a
typed command stream ("slgfx IR") tagged with provenance the game still has
at emission time - which room, which prop, which character. fast3d is
vendored not as our renderer but as our RENDER ORACLE: both consume the same
recordings, and framebuffers are diffed the way `make matching` diffs bytes.
They eyeballed parity; we measure it.

## Measured surfaces (2026-08-21)

| Surface | Size | Shape |
|---|---|---|
| gfx macro emission sites | 1,049 sites in 28 files | per-file conversion waves |
| ...of which 3D core | model.c 173 + bg.c 144 | supervised |
| ...2D/menu/HUD | textrelated 84, front 64, title 57, options 57 | subagent waves |
| ...effects | blood 40, explosion 37, gunfire 36 | subagent waves |
| ...state plumbing | othermodemicrocode 69, tex 47 | informs IR design |
| libultra layering violations | 282 (baseline file) | per-subsystem waves, must reach 0 |
| Gfx* pointer plumbing | 813 refs | retired with the IR |

## Tracks

**T1 - slgfx IR.** A typed command stream replacing Gfx*/F3DEX emission.
Commands carry provenance tags (room id, prop handle, chr handle) stamped at
emission. Two consumers: the parity backend (T2 oracle initially; our own
GL3 backend as it matures) and, in Phase 3, the lighting renderer - which
gets scene semantics for free instead of via a second migration. IR design
is SUPERVISED (judgment, not mechanics). Per-file conversion is subagent
work: 2D wave -> effects wave -> plumbing -> 3D core last. Verification per
file: build both paths, `make trace-verify` (sim untouched - any divergence
is a bug), `make render-verify` once T2 lands.

**T1a - IR forward-compatibility requirements (decided 2026-08-21).**
The IR must be sufficient for a future ray-traced backend and pluggable
upscalers, none of which are Phase 1 or Phase 3 scope but all of which are
cheap now and expensive to retrofit:

- geometry carries STABLE identity (prop/chr/room handle + mesh id), so an
  RT backend can build and refit BVHs instead of rebuilding per frame;
- transforms are world-space and explicit, never pre-baked into vertices;
- material identity is semantic (which surface of which object), with the
  N64 combiner state carried alongside rather than being the only truth;
- backends expose motion vectors, depth and jitter as a contract, making
  DLSS/FSR/XeSS drop-ins rather than integrations.

Precedent that this pays: RT64 (sm64rt, Zelda64Recomp) and RTX Remix spend
most of their complexity RECONSTRUCTING exactly this information from draw
streams. We have it at emission and merely have to not throw it away.
Upscaling itself is uninteresting until path tracing exists - N64 scenes
render at 4K natively - but denoising-grade reconstruction under a future
path tracer is what the contract buys. A light-aware stealth loop (guards
whose perception reads actual light levels, feeding the alarm graph) is the
Nightwatch-facing payoff if Phase 3+ ever goes there; visionrange and
hearingscale are already in every trace.

**T2 - render oracle.** Vendor fast3d (MIT, license retained) driven by our
replay harness: replay a recording, capture framebuffers at fixed ticks
through the oracle and through slgfx, diff. `make render-verify
[LEVEL=]` beside `make trace-verify`. Golden ticks chosen per level from the
coverage data (frames with guards, effects, HUD active). Supervised
bring-up; this also gives us pixels on screen in week one.

**T3 - libultra to zero.** Not PD's permanent shim: per-subsystem conversion
of `src/game` -> `sl_` platform interfaces (sched/thread/message, timing,
PI/DMA->fs, vi, joy, audio-out), each wave shrinking
`docs/layering-baseline.txt` monotonically. The 282 reaches 0 as the gate
says - no reframing needed, because mechanical per-call-site conversion
under a trace gate is exactly what we have that PD did not. Subagent waves;
verification: `make check-layering` count strictly down + trace gate.

**T4 - byte-swap at extraction.** Build-time conversion in the extractor;
runtime big-endian assumptions retired file-by-file behind it. Strategy-
independent; starts immediately. Subagent. Verification: matching build
still MATCHes (extractor output unchanged for the N64 path), trace gate.

**T5 - pointer -> handle.** Per subsystem, starting with chr/prop cross-
references (the ones our schema already watches). KNOWN interaction: the
trace schema hashes raw pointer values (weapon_r/weapon_l, prop unions);
each conversion lands with a documented divergence in docs/divergences.md
and its own re-baseline commit, per project rules. This track is what
Nightwatch's replication stands on - PD's port-net desync list is the
counterexample we cite. Subagent per subsystem.

**T6 - audio.** RSP audio via the sm64 mixer lineage, as PD did - proven,
not worth innovating on. Later in the phase; the sim never depends on it.

**T7 - platform shell.** SDL window/input/timing for the native build,
grown from the harness's recorder (which already speaks SDL video, audio at
measured rates, and DualSense input). Mouselook and modern bindings come
here - informed by the PD port's control scheme, which the owner already
plays comfortably.

## Wave 0 status (2026-08-21)

- Microcode recon SETTLED: stock Fast3D + G_TRI4 only (see slgfx-ir.md).
- Native header compatibility LANDED: gcc -m32 syntax scan of all 134
  src/game files went 1,597 errors -> 16 (7 files), every fix IDO-guarded or
  token-identical, matching build re-verified MATCH. The remaining 16 are
  individual C issues (arg types, a flexible-array init), not systematic.
- First native OBJECT COMPILE landed: 133 of 134 game files produce x86
  objects (-m32). chraidata.c is the one deferral - its AI-script DSL leans
  on IDO's argument-filling three macro layers deep (SWITCH is padded, BREAK
  and the case machinery below it are not) - deferred, not forgotten.
- The link inventory settles the T3 question: src/game references exactly
  TEN libultra functions (osCreateMesgQueue, osGetCount, osInvalDCache,
  osReadHost, osSendMesg, osSyncPrintf, osViBlack, osVirtualToPhysical,
  osWriteHost, osWritebackDCacheAll). The 282-line layering baseline is
  include-level noise by comparison; the real seam is a 10-function shim
  plus everything in src/ outside game/ (boss, sched, joy - counted in the
  501 non-asset externals).
- 576 of 1,077 externals are ROM asset symbols (338 prop models, 92 gun
  models, 80 character models, 54 setups, 12 animation blobs) - exactly the
  T4 transcode surface, now enumerated.
- SKELETON LINKS AND RUNS: `make native-skeleton` compiles 161 files (the
  whole sim plus the decomp's own OS layer - boss, sched, joy, vi, token,
  allocators, all of which turned out to compile natively) against a SEVEN-
  function shim (rmon.c already implements three of the ten) and 984
  auto-generated stubs, into a 1.8MB binary that executes. The stub list is
  the porting worklist, machine-readable in build/native/unresolved.txt.
- Was: libultra shim for the ten, stub the asset symbols, first runnable
  headless skeleton - then the sim can tick natively and trace-verify can
  compare it against the emulator directly.

## Boot bring-up status (overnight 2026-08-22)

The skeleton now BOOTS through real initialization: mainproc -> scheduler
bypass (recv-pump live) -> bossEntry -> debInit -> heap -> langInit loads
and inflates real language files from the ROM -> gu math (compiled from the
repo's own src/libultra/gu) -> weapon animation group resolution.

What it took, each measured not guessed:
- recv-pump works; joy's enable/disable poll handshakes are echoed by the
  pump (the poll loop's own semantics, relocated).
- The game heap is an explicit 16MB native allocation; the N64 derivation
  (bss end to a TLB block off the boot stack) was carving a 4KB stub.
- THE LINKER IS THE ASSET TABLE: the N64 link script places asset data at
  its ROM offset and the game DMAs from symbol ADDRESSES. 827 unresolved
  symbols are now absolutes generated from the N64 map; 26 RAM segments are
  real sized buffers. Stubs fell 984 -> 65, and the remainder are callable
  no-ops because the game CALLS some of them (data-shaped stubs in NX
  memory segfault - guScale taught that).
- SIGSEGV/HANG tracers are built into sl_main (EIP + EBP chain); valgrind
  needs libc6-dbg:i386 when someone is awake to install it.

STOPPED AT the byte-order wall, exactly where this plan predicted: anim
data field reads (model.c:914) interpret big-endian ROM data through
little-endian struct access. This is T4 in earnest - transcode at
extraction, per-format - not a runtime patch. First T4 target list: the
animation header/joint formats initWeaponAnimGroups walks, then model
headers, then setups.

## Sequencing

Wave 0 (now): T4 + T5 begin (strategy-independent); T1 IR design doc
(supervised); T2 vendor + first framebuffer diff.
Wave 1: 2D conversion wave over T1; T3 timing+vi subsystems.
Wave 2: effects + plumbing waves; T3 remainder; T6.
Wave 3: 3D core (model.c, bg.c) supervised; T7 polish; gate run.

The AI track continues in parallel on the matching build throughout - its
corpus (77 sweeps, v10 schema) is unaffected until T5 re-baselines land, and
those are scheduled, documented divergences.

## Phase 1 gate (restated, strengthened)

- All Phase 0 traces pass (as roadmap).
- Native build renders through slgfx with render-oracle parity on the
  golden-tick set for all 20 campaign levels.
- `docs/layering-baseline.txt` reads zero.
- Byte-swap happens only at build time.

## What this buys beyond the port itself

Provenance-tagged IR is the substrate for Phase 3 lighting (clustering by
room, shadow casters by object) and for replay tooling - our recordings plus
the IR make a free spectator/demo viewer a weekend project later, not a
feature. The render oracle methodology extends to Phase 3: every lighting
change diffs against the last-known-good backend the same way.

## Prior-art summary (research 2026-08-21)

PD port (fgsfdsfgs/perfect_dark): libultra shim 8.5KB; fast3d interpreter
106KB MIT; runtime ROM extraction like ours; SP+MP shipped. Its port-net
branch documents bolt-on netcode's failure modes (per-weapon desyncs, "Sims
don't work in netgames", 8-player bandwidth) - the standing argument for
T5-before-replication. Its 60fps/uncap-tickrate work is the Phase 2
reference. What we keep from their playbook: shim shape, extraction-time
byte-swap, mixer lineage, control-scheme reference. What we do differently:
cut at construction not interpretation, oracle-diff parity not eyeballs,
layering to zero not a permanent seam.
