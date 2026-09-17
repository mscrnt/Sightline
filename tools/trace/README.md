# tools/trace — determinism harness (Phase 0)

Deterministic replay and per-tick state hashing for the **matching** build. The
ROM is never modified: state is read from emulator RAM at addresses resolved
from `build/u/ge007.u.map`, so what runs is the byte-identical matching ROM.

## Usage

    tools/trace/trace.py capture --out X.sltrace --level facility --ticks 300
    tools/trace/trace.py diff A.sltrace B.sltrace

## Modules

| file | role |
|---|---|
| `sltrace/symbols.py` | link-map symbol -> RDRAM address |
| `sltrace/state.py`   | state schema, capture and hashing |
| `sltrace/emu.py`     | headless mupen64plus driver |
| `sltrace/traceio.py` | versioned trace file format |
| `sltrace/diff.py`    | divergence localisation and reporting |
| `bootprobe.py`       | ROM reference oracle: boot sequence, frame by frame |

## The weapon oracle (`tools/native/romweapon.py`)

    .venv\Scripts\python.exe tools/native/romweapon.py --stage 36 --weapon sniper --out %TEMP%\sightline-oracle\surface-sniper

What the **cartridge** draws in Bond's hand: the unmodified ROM under the same
core, landed straight in a stage (g_StageNum written into RDRAM over the first
frames - the direct-boot ROMs' own mechanism, without rebuilding an image),
All Guns / Max Ammo switched on the way the cheat menu does, A pressed until
the right hand holds the item, frames saved outside the tree. The native
counterpart is `play.ps1 -Level <name> -Weapon <name> [-Cheats ...]`.

## The reference oracle (`bootprobe.py`)

    .\tools\windows\rom-reference.ps1 -Frames 3000 -Control     # Windows
    python tools/trace/bootprobe.py --frames 3000 --control     # anywhere

Boots the **unmodified** cartridge ROM and walks it one video frame at a time,
recording the game's own counters (`current_menu`, `g_MenuTimer`,
`currentFrameCounter`, `speedgraphframes`, `ninLogoRotRate`, `gunbarrelTimer`,
…) alongside what is actually **on screen**, sampled from the core's video
callback.

It exists because front-end timing kept being "corrected" from threshold
constants in `front.c` and `title.c`, and kept still being wrong when tested.
*How long the Legal screen is visible* is not derivable from a constant: the
visible span includes screen switches and loading that no counter describes.
The state span and the visible span are therefore reported as **separate**
numbers, because the entire disagreement lives in the gap between them.

Two guards are built in, and both have already caught this probe lying:

* `--control` prints every distinct value each field took, so a field that
  never varied is named rather than quietly passing. `gunbarrelTimer` reported
  **1 distinct value** in a 1400-frame window - correctly, since the gun-barrel
  phase had not started yet.
* `presented + carried == frames` is asserted. The first version reported
  "1400 frames, 1327 presented, 0 duped" - three numbers that cannot all be
  true. It counted a NULL-data callback but had no way to see a callback that
  never fired; the arithmetic was the only thing that gave it away.

Output is CSV written **outside** the repository, and a path inside the working
tree is refused - the samples are ROM-derived (project rule 2).
numpy is a fast path, not a dependency; `tests/test_bootprobe.py` asserts the
pure-Python fallback computes identical numbers, and on a real 3000-frame boot
the two produced byte-identical CSVs.

## Four things that are load-bearing

1. **Real gfx and rsp plugins are required.** With null plugins GoldenEye's main
   loop blocks on the graphics pipeline and `currentFrameCounter` freezes near
   50 forever. A harness sampling a hung game looks exactly like a harness
   detecting nondeterminism.
2. **Sample per game tick, never per VI.** The logic loop is gated on
   `osGetCount()`, so VI-keyed sampling lands at a different point inside the
   update each run and reports constant false divergence.
3. **Pure interpreter.** The dynarec is not a determinism guarantee.
4. **RDRAM stores 32-bit words in host byte order.** Every read swaps; skipping
   it yields plausible-looking garbage rather than an obvious failure.

A display is required because a real gfx plugin is required; under CI that means
Xvfb. Hashes are identical with Xvfb and with a real display (verified).

## Traces are environment-specific

Measured 2026-08-18: the same ROM and the same input produce a **different state
hash on the host than in the CI container** - 240 VI per 120 ticks versus 356 -
despite identical mupen64plus and plugin package versions, identical data files,
and software GL forced on both. Each environment is internally deterministic and
reproduces itself exactly; they just do not agree with each other.

Binary hashes of the core and plugins would not catch this, because the packages
are byte-identical in both places. So the environment is named in the trace's
emulator fingerprint, and comparing across environments is refused rather than
reported as a divergence.

The canonical environment is the CI container. Produce traces CI can verify with:

    ci/image/run.sh capture --out /workspace/traces/facility.sltrace --level facility

## Storage strategy

Traces store a composite hash per tick plus one hash per entity - about 24
bytes/tick, so a 10-minute level is under 1 MB. Field-level values are *not*
stored: replay is deterministic, so they can always be recovered by re-running
with detail capture at the divergent tick. Cheap to commit, unlimited diagnostics
on demand.
