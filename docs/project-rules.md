# Project rules — Sightline

The standing rules for anyone contributing to this repository. Other
documents, source comments and tool docstrings cite these by number
("project rules, rule 2"), so the numbering is stable: do not renumber.

`ROADMAP.md` holds the phase structure, gates and the current status. This
file holds the rules.

---

## What this project is

**Sightline** is a modernized engine fork of the GoldenEye 007 N64
decompilation, covering the **full game** — all campaign levels, all
multiplayer maps.

Three goals, in dependency order:

1. **Smarter AI** — squad communication and an alarm information graph, with
   behavior archetypes ported by design from Perfect Dark.
2. **Real lighting** — clustered forward rendering with shadow-casting dynamic
   lights, retaining original vertex colors as baked indirect.
3. **NIGHTWATCH** — asymmetric online mode, 1 infiltrator vs. up to 7 human
   guards, across the campaign.

---

## Non-negotiables

These are not preferences. Violating one invalidates the work.

### 1. The trace harness gates everything

Every change that touches game logic, physics, AI, or entity state must run
`make trace-verify` before being considered complete. A change that has not
run it is not done, regardless of how correct the diff looks.

If traces diverge and the divergence is **intentional**, do not silently
re-baseline. Document the change in `docs/divergences.md` with the tick, the
affected fields, and the justification, then re-baseline in a separate commit
that touches nothing else.

Rationale: parallel work multiplies the surface area for subtle behavioral
regressions. The bottleneck in this project is never authoring speed — it is
diagnosing a system nobody holds a full mental model of. The harness is what
makes that diagnosis take minutes instead of weeks.

### 2. No assets in the repository

Ever. No textures, audio, models, level geometry, or anything derived from
them. Assets are extracted at build or run time from the user's own ROM. If a
change appears to require committing an asset, stop — the task is
misspecified.

This includes AI-upscaled textures. The upscaler ships; its output does not.

### 3. Nothing hand-authored per level

Twenty-plus levels means any manual per-level step becomes the critical path.
Lights, normals, spawn points, guard tables, camera graphs — all must be
**derived by tooling** from existing level data, with a small override file
for corrections.

If an override file exceeds ~30 lines, the deriver is wrong. Fix the deriver.

When a task says "add lighting to Silo," the correct interpretation is almost
always "improve the light deriver until Silo looks right," not "place lights
in Silo."

### 4. Don't merge Perfect Dark source

PD is GoldenEye's descendant, not its sibling; the structures diverged. PD is
a **design reference and behavioral spec source**, never a code dependency.
Reimplement behaviors against Sightline's own AI interface.

### 5. Preserve Rare's behavior by default

The decomp's bugs are Rare's bugs, faithfully reproduced. Do not "fix" them.
Changing them alters game feel and breaks parity with how the game is
remembered.

Fix only: crashes, AI stalls and softlocks, and defects blocking a roadmap
phase. Any intentional behavior change goes behind a runtime toggle,
defaulting to original behavior.

### 6. Stay inside the current phase

Do not perform work belonging to a later phase because it "would be easy
while we're in here." Cross-phase edits are how scope drifts and how
divergences become untraceable. If a later-phase change seems necessary, note
it in `docs/backlog.md` and continue.

### 7. The docs are the authority on formats — consult them first

Zoinkity's GoldenEye and Perfect Dark notes (`kholdfuzion/goldeneye_docs`,
cloned separately alongside this repository as read-only reference) are not
background reading. For any question about a **format, layout, offset,
opcode, byte order, or type code**, the notes are consulted BEFORE the
decomp, and the answer cites a file.

```
python3 tools/docs/gedocs.py for src/game/propobj.c   # what governs it
python3 tools/docs/gedocs.py topic display-lists      # authority for an area
python3 tools/docs/gedocs.py search <terms>           # rank the corpus
```

The decomp tells you what the code **does**, not what the format **is**.
When those disagree — which is exactly what a byte-order or offset bug is —
reading source produces confident, wrong, expensive answers. Measured on
2026-08-24: two published root causes for the collision-DL crash, both wrong,
several hours spent; `ucode05.txt` settled it in under a minute once opened.

Routing from source files to the governing notes lives in
`docs/doc-routing.json`; `docs/decisions/docs-first-enforcement.md` records
how it is enforced.

**Absence claims must cite a tree-wide search.** Saying "X is never swapped",
"nothing handles Y", or "there is no Z" is a claim about the WHOLE
repository, so it requires `grep -rn <thing> src/` over the whole tree — and
the commit or note should say which search came back empty. On 2026-08-24
this rule would have prevented three wrong conclusions in one investigation:
"pads are never byte-swapped" came from grepping one file while `prop.c` had
swapped them all along, bounded by a helper implementing the very derivation
being proposed as novel. The error is not forgetfulness — each search felt
complete from the inside — so the guard has to be a citable artefact, not a
resolution to be careful.

Corollary, measured the same day: every MEASUREMENT taken held up; every
conclusion reached by reading code was wrong. When both are available,
measure.

**Feed it back.** When a note answers a question, add the mapping to
`docs/doc-routing.json`. When the notes are searched and found silent, add a
`not_covered` entry so nobody re-searches a dead end — several are already
recorded, including the alarm system (original work, not a port) and the AI
command lists (only `chraidata.c`).

Two standing cautions: `Rand++ Documentation (Misc)/` describes a ROM hack's
MODIFIED formats and is never authority for the base game (the index excludes
it), and Perfect Dark remains a behavioural reference only — see rule 4.

---

## Explicitly out of scope

Do not do these, even if they look like obvious improvements:

- **Codebase-wide cleanup.** The IDO-appeasing unions, hoisted temporaries,
  and permuter artifacts stay unless the file is being rewritten for another
  reason. Zero player-visible payoff, high divergence risk.
- **Blanket fixed-point → float conversion.** Convert only inside renderer
  and physics paths where it buys something concrete.
- **RCP emulation or static recompilation.** Wrong architecture for this
  project.
- Renaming, reformatting, or restructuring files not otherwise being touched.
- Adding dependencies without asking. See Dependencies below.

---

## Conventions

### Build and verify

```
make matching        # original matching build (reference oracle)
make sightline       # modernized fork
make trace-verify    # replay all recorded traces, diff state hashes
make trace-verify LEVEL=facility    # single level, faster iteration
make trace-record LEVEL=<name>      # record a new input stream
make trace-roundtrip                # prove record->replay is faithful
```

The native Windows build has its own entry points; see `readme.md` and
`docs/toolchain.md`.

### Layout

```
src/game/       game logic — AI, physics, objectives (existing decomp code)
src/libultra/   existing — N64 SDK, deleted or stubbed during Phase 1
src/gfx/        gfx_ interface and backends (new)
src/net/        replication, prediction, server (new)
src/platform/   thin platform layer — window, input, audio (new)
tools/trace/    determinism harness (new)
tools/derive/   per-level derivers: lights, normals, spawns, guard tables (new)
data/overrides/ hand-tuned per-level corrections (keep small)
docs/           divergences.md, backlog.md, layering-baseline.txt, decisions/
```

This repo **is** a fork of `gitlab.com/kholdfuzion/goldeneye_src`, not a
wrapper around it. `upstream` points at the original so their fixes can be
rebased in. Zoinkity's GE and PD notes (`kholdfuzion/goldeneye_docs`) are
cloned separately as read-only reference.

**Layering goal:** `src/game/` must not include from `src/gfx/`,
`src/platform/`, or the libultra/RCP layers. This is what makes the AI track
independently shippable and the server build headless.

It is **not true today** — the decomp's `src/game` calls into libultra
directly, and severing it is Phase 1's work. `make check-layering` therefore
fails only on violations absent from `docs/layering-baseline.txt`. The
baseline count must decrease monotonically and reach zero at the Phase 1
gate. Never add to the baseline to make a build pass; if a change would
introduce a new violation, the change is wrong.

### Naming

- Original decomp symbols keep their names. Do not rename to "improve
  clarity" — they are the shared vocabulary with the upstream project and its
  documentation.
- New subsystems use the `sl_` prefix.
- The engine is `sightline`. The asymmetric mode is `Nightwatch`. Keep them
  distinct in code, docs, and commits.

### Commits

One logical change per commit. Trace re-baselines are always their own
commit. Reference the roadmap phase:
`[P3] derive: infer lights from emissive surfaces`.

### Dependencies

Prefer none. The platform layer should be thin enough to swap. Do not pull in
a framework to solve a problem a few hundred lines would cover. Ask before
adding anything that links.

### Delegated and parallel work

Well-specified, testable, parallel tasks suit independent contributors:
individual `gfx_` call-site conversions, pointer → handle conversion per
subsystem, deriver tooling with clear input/output contracts, AI behavior
archetypes written against a fixed interface, asset extraction and byte-swap
tooling.

Tasks that need one supervised, coherent pass: anything that re-baselines
traces, the sim/render decoupling (Phase 2), balance and feel tuning, and
anything where "correct" is a judgment call rather than a test.

**Every delegated task must state its verification step.** A task without one
gets sent back rather than merged.

---

## Glossary

- **Matching build** — the original decomp, compiled to byte-identical MIPS.
  The correctness oracle. Kept buildable permanently.
- **Trace / oracle** — recorded input stream plus per-tick state hashes used
  to prove behavioral equivalence.
- **Divergence** — a trace mismatch. Intentional ones are documented and
  re-baselined; unintentional ones are bugs.
- **Alarm graph** — the information network of guards, cameras, radios, and
  alarm panels. Guards know only what has been communicated to them.
- **Deriver** — tooling that generates per-level data from existing level
  data.
- **Nightwatch** — the asymmetric mode. 1 infiltrator, up to 7 human guards.

---

## Current status

See `ROADMAP.md`. The vertical-slice target is Facility, but the full game is
the shipping scope: the slice is judged on whether the derivers generalize,
not on how good Facility looks. If Facility looks wrong, fix the deriver,
never the level.
