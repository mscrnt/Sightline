# slgfx IR — design notes (T1, in progress)

Status: DESIGN. Wave 0 recon findings that constrain the design, recorded
before any code.

## Microcode recon (settled 2026-08-21)

GoldenEye builds against the VANILLA Fast3D branch of stock gbi.h - no
F3DEX_GBI define exists in the build. The RSP side is Rare's "Microcode05 /
RSP SW 2.0G" (rsp/graphics/gmain.s, in-tree), and Zoinkity's ucode05 notes
give its full command table: stock Fast3D encodings throughout, with exactly
ONE Rare extension - G_TRI4 at 0xB1 (four triangles per command, where F3DEX
would put G_TRI2) - plus sprite2d (09), which Fast3D already defines. The
combiner quirk Zoinkity notes (MultiGen emitting wrong combiner words) is
fixed by the GAME at load via lookup table, so every stream we intercept is
post-fix.

Consequence for T2: the sm64-lineage gfx_pc already speaks Fast3D; the
oracle needs a one-opcode decoder extension for TRI4. The risk that GE spoke
an alien dialect is retired.

## Two kinds of display list - two different cuts

1. **Dynamically constructed DLs** (1,049 macro sites, 28 files): HUD, menus,
   effects, sky, debug. These become slgfx IR emission at the call site -
   the T1 conversion waves.
2. **ROM-baked asset DLs** (models, level geometry): these are DATA. The
   game itself parses them at runtime today (bg.c, propobj.c, lightfixture.c
   switch on opcodes to pull vertices for collision and lighting). The port
   strategy: TRANSCODE them to IR meshes at extraction time (T4), stamped
   with provenance then. The shipping path never walks Fast3D at runtime;
   the oracle backend still can, for parity diffing.

This split is what the PD port could not do - their interpreter treats both
kinds identically at runtime, which is exactly why their renderer has no
scene semantics.

## Requirements carried from the plan of record

Stable geometry identity (BVH refit), explicit world-space transforms,
semantic material identity with combiner state alongside, and a backend
contract exposing motion vectors + depth + jitter. See
phase1-tracks.md, T1a.

## Open design questions (supervised)

- IR granularity: retained meshes + per-frame command buffer, or fully
  immediate? (Leaning retained for assets, immediate for HUD.)
- Matrix stack semantics: mirror gSPMatrix push/pop or resolve to flat
  world transforms at emission? (Flat, per T1a - but sky and viewmodel
  passes need care.)
- How the oracle taps the stream: RDRAM DL capture at golden ticks from
  the existing replay harness vs native-build capture. RDRAM capture works
  BEFORE any native build exists - likely the Wave 0/1 bridge.
