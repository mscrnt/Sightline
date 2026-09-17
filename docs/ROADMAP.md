# SIGHTLINE — Roadmap

A modernized engine fork of the GoldenEye 007 N64 decompilation, targeting the
**full game**: all campaign levels, all multiplayer maps.

Three deliverables, in dependency order:

1. **Smarter AI** — squad communication, an alarm information graph, and simulant
   archetypes ported by design from Perfect Dark.
2. **Real lighting** — clustered forward renderer, shadow-casting dynamic lights,
   original vertex colors retained as baked indirect.
3. **NIGHTWATCH** — asymmetric online mode, 1 infiltrator vs. up to 7 human guards,
   playable across the campaign.

---

## Scope decision: full game

Full-game scope is the stated goal. It is achievable, but it changes the shape of
the project in one specific way, and every phase below is designed around it:

> **Nothing may be hand-authored per level.**

Twenty campaign levels plus MP maps means any manual step — placing lights,
tagging normals, defining spawn points, writing guard tables — gets multiplied by
twenty-plus and becomes the critical path. Every per-level artifact must be
**derived automatically from existing level data**, with a small hand-tuned
override file for the cases the deriver gets wrong.

The rule of thumb for every task in this project:

> If it must be done per level, build the tool first. Manual passes are for
> overrides only, and an override file that exceeds ~30 lines means the deriver
> is wrong and should be fixed instead.

Recommended ordering within full-game scope: get **one** level fully through the
pipeline end to end (Facility is the natural candidate — dense geometry, varied
lighting, good MP layout), then run the remaining levels through the *same tools*
in batch. This is not a reduction in scope. It is validating the pipeline before
paying for it twenty times.

### Milestone: VERTICAL SLICE (Facility)

The demo target. Full game remains the shipping scope; this is the checkpoint that
proves the pipeline before it is paid for twenty times.

**Contents**

- Facility at 1080p/60 with derived lighting and shadow-casting muzzle flashes.
- Full AI overhaul active — squad communication, alarm information graph.
- Nightwatch playable at 1v3.
- No upscaled textures. Original assets, correctly rendered.

**Exit criteria — read carefully**

The slice is not judged on how good Facility looks. It is judged on whether the
tooling generalizes:

1. Facility required **zero hand-placed lights**; its override file is under 30
   lines.
2. A second level (Runway, or another with a different lighting character) runs
   through the **untouched** derivers and reaches acceptable quality in under a
   day of work.
3. Nightwatch's guard tables, camera nodes, and objective clocks for both levels
   were derived, not authored.
4. All Phase 0 traces still pass.

**Failure mode to avoid**

Hand-tuning Facility until it is beautiful. That produces a great demo and teaches
nothing about the remaining levels, and the work does not transfer. If Facility
looks wrong, fix the deriver — that is the entire point of the milestone.

Criterion 2 is the real gate. Everything else is table stakes.

---

## Phase 0 — The oracle

**Nothing else starts until this is green.**

Leaving the matching build means losing the ability to prove correctness by
comparing assembly. The replacement must exist before the divergence, not after.

### Deliverables

- `tools/trace/` — deterministic replay harness running on the **matching** build.
- Recorded input streams for every campaign level (a full playthrough per level,
  plus adversarial recordings: alarms triggered, all guards killed, objectives
  failed, mine chains, body-pile pathing).
- Per-tick state hash: entity positions and rotations, health, weapon and ammo
  state, RNG cursor, alarm and objective flags, animation frame indices.
- `trace-diff` — reports the **first divergent tick** and which state fields
  differ, not just a pass/fail.

### Gate

Every campaign level replays byte-identically across 10 consecutive runs on the
matching build. Traces are committed as build artifacts and versioned.

### Why this is non-negotiable

Agent-parallelized work multiplies the surface area of subtle behavioral
regressions. Speed of authoring is not the bottleneck; speed of *diagnosis* is.
The harness is the only thing that turns "something feels wrong in Silo" into
"tick 4,412, guard 7's pathfinding state diverged."

---

## Phase 1 — Excise the RCP

Cut cleanly between *what to draw* and *how to draw it*.

### Keep

- Room graph and portal culling — this is good spatial structure and it is reused
  later for light clustering and shadow-caster culling.
- Visibility determination, LOD selection, object culling.

### Delete behind a `gfx_` interface

- Display list construction and F3DEX microcode assembly.
- TMEM tile loading and the 4KB texture cache dance.
- Cartridge DMA, framebuffer swaps, VI configuration.

### Also in this phase

- **Byte-swap assets at build time.** Never at runtime. Big-endian assumptions are
  baked into every asset and struct; converting once during extraction is the only
  sane approach.
- **Pointer → handle.** Entity references stored as raw `u32` pointers must become
  stable integer handles. Unglamorous, and it blocks both networking and save
  compatibility until done.

### Explicitly NOT in this phase

- Codebase-wide fixed-point → float conversion.
- Rewriting IDO-appeasing unions, hoisted temporaries, and permuter artifacts
  outside the files being touched anyway.
- General beautification.

These have zero player-visible payoff and every one is a divergence risk. Convert
to float only inside the renderer and physics paths where it buys something.

### Gate

All Phase 0 traces still pass. Game renders through a null/software `gfx_` backend
at parity with the matching build.

---

## Phase 2 — Decouple sim from render

Fixed 60Hz simulation tick. Inputs enter as command structs. Render interpolates
between ticks.

This single refactor is the prerequisite for **both** high-framerate rendering and
networking. Doing it once, properly, here saves doing it badly twice later.

### Note on parity

This is the point where bit-exact parity with original hardware ends, permanently.
The original is welded to a vsync-locked ~20–30fps tick with frame-budget hacks.
Trace comparison after this phase validates *behavioral equivalence at tick
boundaries*, not identical frame timing. Update the harness accordingly and
document the new baseline.

### Gate

Traces pass under the tick-boundary comparison mode. Frame rate uncapped and
decoupled; gameplay speed unchanged at 30, 60, 144, and 240 fps.

---

## Phase 3 — Lighting and rendering

### Core insight

**N64 vertex colors are baked lighting, not vertex paint.** Do not discard them.
Retain them as an ambient/indirect term and multiply dynamic lights on top. This
preserves Rare's original art direction as free global illumination — better than
anything that would be authored from scratch, and it is what keeps twenty levels
looking coherent without twenty lighting passes.

### Renderer

**Clustered forward, not deferred.** Geometry is low-poly so the project is never
geometry-bound; MSAA stays cheap; and the game is full of alpha-tested surfaces
(fences, grating, glass, foliage, muzzle flashes) that deferred handles badly.

### Ordered by visible impact

The biggest wins are not texture work. Ranked by visible improvement per unit of
effort, and every item here is **asset-free** — nothing below requires remaking
or shipping art.

**Free and immediate**

- **1080p and a 32-bit framebuffer.** Removes the dithering pattern baked into
  every N64 image. A bigger perceived change than any texture work. (Listed under
  Required below.)
- **Real bilinear/trilinear filtering, anisotropic, and true mipmaps.** The N64
  used 3-point triangular sampling — a hardware shortcut — and had no room for
  mipmaps in 4KB of texture memory. Proper filtering removes shimmer on floors
  and long corridors.
- **Alpha-to-coverage.** Fences, grating, foliage, railings. The single
  highest-value toggle in the renderer. (Required, below.)
- **MSAA.** Geometry is low-poly, so 8× is close to free.
- **Lock LOD to maximum.** The game swaps to cruder models at distance to save a
  budget that no longer exists. Simply stop.
- **Push draw distance,** keeping fog as an art option rather than deleting it.

**Lighting — the actual transformation**

- **Per-pixel rather than per-vertex lighting.** Same geometry, same lights,
  dramatically better result. Depends on generated normals.
- **Muzzle flashes as shadow-casting lights.** Facility during a firefight
  becomes unrecognisable. (Required, below.)
- **SSAO.** Punches far above its weight on low-poly interiors: grounds objects
  and gives corners and doorframes real depth. No assets involved.
- **Bloom and proper tonemapping** on lights and muzzle flashes.
- **Original vertex colours as the indirect term** — Rare's hand-authored
  lighting working as free global illumination. (See Core insight.)

**Between categories**

- **Normal maps derived from existing texture luminance.** No new art, and tiled
  surfaces — brick, grating, concrete — start reading as actual relief. Works
  well on architecture and badly on anything with detail painted in, so gate it
  per material class.

### Required

- **Alpha-to-coverage.** The 32×32 alpha-tested textures alias violently at 1080p.
  This matters more to perceived quality than any texture upscale.
- **Muzzle flashes as real shadow-casting lights.** The data already exists as
  per-vertex dynamic lighting. Promoting it is the single highest-impact visual
  change in the project.
- **Automated normal generation.** Much of the geometry carries no normals.
  Smoothing-angle threshold per surface material class, derived automatically,
  with a per-level override file for the handful that look wrong.
- **Automated light derivation.** Lights must be inferred from emissive texture
  surfaces and analysis of the baked vertex colors — not placed by hand. Per-level
  override file for corrections. This is the highest-leverage tool in the project;
  without it, Phase 3 does not scale to twenty levels.
- **Portal structure as light clustering.** The room graph is already a spatial
  subdivision. Use it directly for light probe volumes and shadow-caster culling
  rather than building a new one.
- **32-bit color.** Drop the 16-bit dithered framebuffer.

### Preserve as art options

- **Fog.** Dam, Surface, and Statue are composed around it. Deleting fog is not
  modernization. Keep the original curves selectable and default them on.
- Original filtering and dithering, as a toggle.

### Assets last

Texture upscaling comes after the renderer is stable. Engine churn invalidates
asset pipelines; starting here means rebuilding the texture pass three times.

It also ranks last on merit, not just sequencing. **Detailed textures on
300-triangle models widen the fidelity mismatch.** The original art reads well
partly because everything is uniformly low-fi; lighting and filtering improve the
image without breaking that coherence, while upscaled textures on unchanged
geometry can actively hurt it. Offer a sharp-bilinear option too — some players
prefer crisp original pixels to smoothed inference.

When it happens: de-palettize first, process alpha as a separate pass to avoid
halos, and expect poor results on the guard face textures (photographs of Rare
staff — upscalers hallucinate badly on faces). Hand-select those.

### Gate

All levels render at 1080p/60 with derived lighting and no per-level manual light
placement beyond override files. Visual regression screenshots captured per level.

---

## Phase 4 — Netcode

There is no netcode to modernize. The original is splitscreen: one process, four
local pads, shared globals read directly.

### Architecture

**Authoritative client-server with snapshot interpolation.** Not rollback —
full state rewind at 8 players is brutal, and Quake-style snapshot interpolation
matches deathmatch far better.

### Required

- **Server-side lag compensation on hit traces.** Non-negotiable. The game is
  hitscan with instant headshot kills; without rewind on the server, 8-player
  online is unplayable at 60ms.
- Entity replication built on the Phase 1 handle system.
- Client prediction for local movement only.
- Dedicated headless server build.

### Level work

MP maps ship with four spawn points. Eight players needs more, plus reworked item
placement. **Derive spawn candidates automatically** from navmesh coverage and
line-of-sight analysis, then curate. Hand-placing spawns across every MP map is
exactly the kind of manual per-level work that must be tooled.

### Gate

8 players, all MP maps, sustained 60Hz server tick, playable at 100ms RTT.

---

## Phase 5 — NIGHTWATCH

One infiltrator running a campaign objective; up to seven human players as
security, respawning into available guards.

### The alarm information graph

The design core, and the reason this mode is more than a novelty.

Guards know only what has been **communicated to them**. Cameras, alarms, radios,
and line-of-sight form an information network:

- A guard who observes the infiltrator holds that knowledge locally.
- Propagation requires a channel: shouting range, a radio, reaching an alarm panel.
- Cameras feed a monitored node; the monitor must relay.
- The infiltrator's counterplay is **severing the network** — cutting cameras,
  isolating guards, suppressed weapons, taking out radio operators first.

This system serves double duty: it is also what makes single-player stealth
better, so it pays for itself twice.

### Balance

1vN asymmetric balance is genuinely hard, and the guard side must be fun while
dying constantly. Perfect Dark's respawn-as-anyone answer is elegant but untested
at this scale.

**Prototype at 1v3 before tuning for 1v7.** This is a balance-discovery step, not
a scope reduction — full-game rollout follows once the ratio curve is understood.

### Per-level requirements (must be derived, not authored)

- Guard respawn eligibility tables from existing enemy spawn data.
- Objective clocks and infiltrator win conditions from the existing objective
  bytecode.
- Camera and alarm node graph extracted from level data.

### Gate

Campaign levels playable as Nightwatch with derived tables and no per-level
scripting.

---

## Parallel track — AI overhaul (starts immediately)

**Not gated on any of the above.** Guard behavior is pure game logic — it runs on
the matching build, on real N64 hardware. It touches nothing being deleted in
Phase 1, so it merges cleanly later.

Start this on day one, in parallel with Phase 0's harness work.

### Work

- Squad communication and knowledge propagation (the alarm graph, single-player
  form).
- Perfect Dark simulant behavior archetypes — **ported by design, reimplemented
  against Sightline's own AI interface.**
- Search patterns, investigation of bodies and noise, flanking, suppression.

### On Perfect Dark

Do **not** merge codebases. PD is GoldenEye's descendant, not its sibling; the
structures diverged enough that a merge is a swamp. PD is a design reference and
a behavioral spec source, never a dependency.

### Shippable independently

This track can ship as a standalone AI overhaul on the matching build long before
the engine work lands. Worth doing — it generates feedback early.

---

## Explicitly out of scope

- **Bug-fixing as a goal.** The decomp's bugs are Rare's bugs, faithfully
  reproduced. Fixing them is not neutral: it changes feel, breaks speedrun parity,
  and some are load-bearing to how the game is remembered. Fix only: crashes,
  AI stalls/softlocks, and anything blocking a phase. Everything else is preserved
  behavior, and any intentional change goes behind a toggle.
- Codebase-wide cleanup and beautification.
- RCP emulation / static recompilation approaches.
- Merging Perfect Dark source.
- Shipping any original or derived asset. See Legal.

---

## Distribution and legal posture

### What ships

A native executable — no emulator, no ROM at runtime. The `sightline` binary,
the upscaler and its weights, shaders, and config. All original code.

**Never shipped:** textures, audio, models, level data, or anything derived from
them, including AI-upscaled textures.

### First-run flow

1. User selects their own ROM dump.
2. **Auto-detect and convert byte order.** Accept `.n64` (word-reversed), `.v64`
   (byte-swapped pairs), and `.z64` (native) by reading the header magic —
   `40123780`, `37804012`, `80371240` respectively. Never trust the file
   extension; it is wrong constantly. This is the single largest source of
   support burden in ROM-dependent ports, and it must fail with a readable
   message ("this appears to be a PAL dump; NTSC US is required"), never a stack
   trace.
3. Verify SHA-1 against `abe01e4aeb033b6c0836819f549c791b26cfde83` (NTSC US).
4. Extract assets to a local user directory.
5. Optional GPU upscale pass, ~2 minutes.
6. Subsequent launches go straight to the game.

Precedent: Ship of Harkinian, sm64ex, and the Doom 64 port all use this model.

### Interim distribution

Before Phase 1 lands, the AI track ships as a `.z64` ROM hack requiring an
emulator or flash cart, since it runs on the matching build. Worth doing — it
gets the alarm graph in front of players long before the engine work is ready.

### Personal builds

`make personal` produces `sightline-personal` with assets extracted and embedded
at build time, skipping the first-run wizard. Used for local development and
iteration — it removes the extraction step between rebuilds and will be the daily
driver from Phase 3 onward.

This must be a **build-time flag, never a separate code path.** Both targets
compile identical game code; only asset provisioning differs.

Required guards:

- Distinct output name (`sightline-personal` vs `sightline`) so the two are never
  confused at release time.
- The release target **hard-errors** if `PERSONAL=1`. Not a warning.
- CI never builds this target.
- **No ROM or extracted asset may ever leave the machine that owns it.** Nothing
  ROM-derived is archived as a build artifact, printed to a log, or placed on
  shared or cloud CI infrastructure.
- ROM-dependent jobs (`matching`, `trace-verify`) run only on a **self-hosted
  runner on hardware the developer owns**, reading their own dump from a path
  outside the workspace, mounted read-only, on a runner that serves no other
  users. This is what keeps the Phase 0 gate machine-verifiable rather than a
  manual ritual.
- `sightline-personal` and all embedded asset blobs are gitignored.

A personal build is for the machine that built it. Handing the binary to anyone
else is distribution regardless of intent — playtesters run their own extraction
from their own dumps.

### Rules

- **Code only in the repository.** No assets, ever.
- **Upscaling runs client-side.** Ship the upscaler, not its output. Upscaled
  textures are derivative works of Rare's art; generating them on the user's
  machine from the user's ROM means nothing infringing is distributed.
- No use of Bond, 007, character names, or Rare/Nintendo marks in project naming,
  branding, or repository metadata.
- `SIGHTLINE` has not been cleared for trademark conflicts. At least one drone
  software company uses the term, though not in games. Clear before registering
  anything.
- Replacing all assets with originals (the Freedoom approach) would remove the
  ROM dependency entirely. It is a multi-year art project and out of scope; do
  not let it creep in.

---

## Platform

Develop on **Linux**. The decomp toolchain (`ido-static-recomp`, `splat`,
`asm-differ`, the permuter, MIPS binutils) is POSIX-native, and case-sensitive
filesystem behavior matters for generated symbol files.

Cross-compile releases with `zig cc` — Linux → Windows and macOS from one machine,
no toolchain zoo.

WSL2 is viable for building only; gamepad passthrough and audio latency make it
unusable as a test target. Keep a Windows machine or dual-boot for release testing.

Windows is needed only for PIX/Nsight GPU profiling late in Phase 3. RenderDoc on
Linux covers the other 95%.
