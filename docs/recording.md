# Recording traces

How to capture a level and prove it replays. The Phase 0 gate is every
campaign level replaying byte-identically across ten consecutive runs.

## Per level

```
make trace-record LEVEL=<name>     # play it; close the window to stop
make trace-verify LEVEL=<name>     # replay the recording, diff state hashes
```

`trace-record` builds a ROM that boots straight into the mission on Agent, so
there are no menus to walk. The first run for a level takes about a minute to
build it; after that it is cached in `build/u/direct/` and reused.

Recording writes three files, and all three are needed to reproduce the run:

| File | What it is |
|---|---|
| `tools/trace/inputs/<name>.input` | your controller input, 4 bytes per frame |
| `tools/trace/inputs/<name>.eeprom` | the cartridge save the session started from |
| `tools/trace/traces/<name>.sltrace` | per-tick state hashes - the baseline |

## Proving the set

```
make trace-gate-status        # what is passing, what is not
make trace-gate               # gate everything that needs it
make trace-gate RUNS=3        # a quicker pass while iterating
```

`trace-verify` replays a level once. The gate replays it ten times, and the
difference matters: both real failures this harness has caught were invisible
to a single comparison. mupen64plus reproduced roughly one replay in four, and
the save write-back bug passed run 1 and only then corrupted the input for
run 2. One clean pass is not evidence.

Results are keyed to the content of a recording - input stream, snapshot and
baseline. A level that has passed is skipped until one of those changes, so
adding levels costs only the new ones, and re-recording a level invalidates
exactly that level. Running the gate over a finished set is free.

`gate-results.json` is committed, and CI checks it: a recording that reaches
the repository without a current gate result fails the build. Otherwise an
ungated recording looks exactly like one that passed.

## Levels

| 1 | Dam | 33 | `make trace-record LEVEL=dam` |
| 2 | Facility | 34 | `make trace-record LEVEL=facility` |
| 3 | Runway | 35 | `make trace-record LEVEL=runway` |
| 4 | Surface 1 | 36 | `make trace-record LEVEL=surface` |
| 5 | Bunker 1 | 9 | `make trace-record LEVEL=bunker1` |
| 6 | Silo | 20 | `make trace-record LEVEL=silo` |
| 7 | Frigate | 26 | `make trace-record LEVEL=frigate` |
| 8 | Surface 2 | 43 | `make trace-record LEVEL=surface2` |
| 9 | Bunker 2 | 27 | `make trace-record LEVEL=bunker2` |
| 10 | Statue | 22 | `make trace-record LEVEL=statue` |
| 11 | Archives | 24 | `make trace-record LEVEL=archives` |
| 12 | Streets | 29 | `make trace-record LEVEL=streets` |
| 13 | Depot | 30 | `make trace-record LEVEL=depot` |
| 14 | Train | 25 | `make trace-record LEVEL=train` |
| 15 | Jungle | 37 | `make trace-record LEVEL=jungle` |
| 16 | Control | 23 | `make trace-record LEVEL=control` |
| 17 | Caverns | 39 | `make trace-record LEVEL=caverns` |
| 18 | Cradle | 41 | `make trace-record LEVEL=cradle` |
| 19 | Aztec | 28 | `make trace-record LEVEL=aztec` |
| 20 | Egyptian | 32 | `make trace-record LEVEL=egypt` |

Not campaign missions, so not part of the gate: `citadel` (40), `complex` (31), `temple` (38).

The IDs are internal map ids, not campaign order - Bunker 1 is 9 while its
neighbours are in the twenties and thirties. Never compute one from a level's
position.

## Supporting commands

```
make direct-boot LEVEL=<name>   # build just the ROM, without recording
make trace-unlock               # seed the save so every stage is selectable
make trace-roundtrip            # prove record->replay is faithful (no ROM edits)
make matching                   # rebuild the oracle, byte-identical
```

## Things worth knowing

**Difficulty is Agent, automatically.** `bossMainloop` sets it when the stage
is not the title. There is no flag to pass.

**The matching build is always restored.** `build/u/ge007.u.z64` is what the
harness reads by default, so `direct-boot` rebuilds the oracle when it is
done. Leaving a direct-boot ROM there would quietly record later traces
against the wrong image.

**Replay picks its ROM by hash.** A trace records the ROM it was captured
with, and replay chooses the image that matches. Levels recorded before
direct-boot existed keep verifying against the matching ROM without any
special casing.

**Do not run two replays at once.** They are separate processes and will not
corrupt each other, but they compete for CPU and a recording session will
stutter.

**A save mismatch is reported as a save mismatch.** If a replay starts from
different progress the diff says `different cartridge save` rather than
pointing at a tick. That distinction cost a long debugging detour once; if you
see it, the snapshot and the recording have come apart.


## Recording a LIVE native session (SL_INPUT_RECORD)

`make trace-record` captures through the emulator harness. That cannot reproduce
what a player actually did in the native build, which is the thing worth
replaying when they hit something.

    SL_ROM=baserom.u.z64 SL_BOOT_LEVEL=34 SL_WINDOW=1 \
      SL_INPUT_RECORD=/tmp/session.input ./build/native/skeleton

Replay it anywhere, including headless under a virtual display:

    xvfb-run -a --server-args="-screen 0 640x480x24" \
      env SL_ROM=baserom.u.z64 SL_BOOT_LEVEL=34 SL_WINDOW=1 \
          SL_INPUT=/tmp/session.input SL_FRAMES=20000 \
          ./build/native/skeleton

**The run also records its RNG seed.** boss.c seeds the game RNG at boss
start, and every draw from the level load onward hangs off that one state -
the intro camera pick, the spawn look angle, AI reaction timers. The input
stream alone therefore does not describe its own run. The state is appended to
the run directory's `env` sidecar as `SL_RNG_SEED=<16 hex digits>` once the
game has committed to it, and printed on stderr on every native launch. Replay
it by putting that variable back:

    SL_RNG_SEED=<value> SL_ROM=baserom.u.z64 SL_BOOT_LEVEL=34 \
      SL_INPUT=<run>/input SL_VIS=<run>/input.vis ./build/native/skeleton

It is the 64-bit RNG STATE, not the seed argument: `randomSetSeed(x)` stores
`(s64)(s32)x + 1`, so the argument is not recoverable from the state and the
state is what the next `randomGetNext()` consumes.

**Recordings made before 2026-08-28 have no `SL_RNG_SEED` line, and that is
handled rather than ignored.** Nothing reconstructs the state after the fact -
it is not derivable from the frame count or the input stream. A replay with no
seed supplied falls through to the deterministic boot default, which is the
seed those runs were recorded on, so the existing corpus keeps replaying
exactly as it did.

**Interactive launches vary their seed; replays do not.** An ordinary native
launch draws a boot seed from the host clock, so the game varies between runs
the way the cartridge does - the facility intro camera is the visible example.
A replay never does: an explicit `SL_RNG_SEED` always wins, and a replay
without one keeps the deterministic default rather than being handed fresh
entropy.

**It is flushed every read**, so a crash or a hang keeps the tail - which is
exactly the part worth replaying.

**A recording is NOT a byte-copy of a replayed stream, and that is correct.**
The stream advances once per video frame while the game may read the controller
a different number of times, so reads and stream positions are not 1:1. The file
records WHAT THE GAME SAW. Verified by behaviour rather than bytes: replaying a
recording of `facility-fire.input` fires 14 rounds over 1500 frames, exactly as
the original stream does over the same window.

**Size:** 4 bytes per controller read, so roughly 240 KB per minute at 60Hz -
keeping the last few levels around costs a few megabytes. They are inputs, not
ROM data, so they are safe to keep in the repo (`tools/trace/inputs/` already
holds 119).

**Why this exists.** Every bug chased on 2026-08-25 was harder without it:
synthesised streams reached the airlock and stopped, while the owner walked
into the scientist section and hung the game inside a minute. A recording of
that session would have turned a three-crash hunt into one replay.
