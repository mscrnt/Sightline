# Toolchain

Everything needed to reproduce the Sightline build environment from a bare
machine, and the reasoning behind the non-obvious parts.

Verified working on 2026-08-18, producing a byte-identical US ROM.

---

## 1. Verified environment

The exact versions this was reproduced on. Newer versions of the host compiler
and binutils are very likely fine — they build the *tools*, not the ROM. The ROM
itself is compiled by the vendored IDO compiler, which is pinned and is the only
component whose version can change the output bytes.

| Component | Version | Source |
|---|---|---|
| OS | Ubuntu 24.04.2 LTS | — |
| Kernel | 6.18.33.2-microsoft-standard-WSL2 | WSL2 |
| GNU Make | 4.3 | `build-essential` |
| gcc / g++ | 13.3.0 (Ubuntu 13.3.0-6ubuntu2~24.04) | `build-essential` |
| MIPS binutils | 2.42 (`2.42-2ubuntu1cross5`) | `binutils-mips-linux-gnu` |
| Python | 3.12.3 | system, `/usr/bin/python3` |
| git | 2.43.0 | — |
| IDO compiler | `decompals/ido-static-recomp @ d5aec59` | vendored, `tools/ido5.3_recomp/` |
| rabbitizer | subrepo commit `72bf240f468d30286888212b5fb773fae94340f6` | vendored |
| Upstream decomp | `c4356466` (2026-08-17) | `gitlab.com/kholdfuzion/goldeneye_src` |

---

## 2. System packages

These stay on the host via apt — they are **not** in the venv.

```bash
sudo apt-get install -y \
    build-essential \
    binutils-mips-linux-gnu \
    pkg-config \
    libcapstone-dev
```

Exact versions verified:

```
build-essential          12.10ubuntu1
binutils-mips-linux-gnu  2.42-2ubuntu1cross5
pkg-config               1.8.1-2build1
libcapstone-dev          4.0.2-5.1build1
```

### Notes on two of these

**`binutils-mips-linux-gnu` is required and load-bearing.** The Makefile probes
for a MIPS toolchain prefix in this order: `mips-linux-gnu-`, then
`mips64-linux-gnu-`, then falls back to `mips64-elf-`. It assembles the hand-written
`.s` files and links the ELF. Without it the build fails immediately.

**`libcapstone-dev` is vestigial.** The vendored `tools/ido5.3_recomp` replaced
capstone with rabbitizer in the decompals rewrite (2022-10-16), and nothing in
the current build links against capstone. It is listed here because it is part of
the documented decomp environment and is harmless, but a working build does not
require it. Do not spend time on it if it is awkward to install.

**`python3-venv` is *not* needed on this host.** Ubuntu 24.04's Python 3.12.3
ships a working `ensurepip`, so `python3 -m venv` succeeds without it. If
`make venv` fails with a message about `ensurepip`, then install
`python3-venv` (or `python3.12-venv`) and retry.

---

## 3. Python

All Python tooling lives in a venv at the repo root:

```bash
make venv
```

which is equivalent to:

```bash
python3 -m venv .venv
.venv/bin/python3 -m pip install --upgrade pip
.venv/bin/python3 -m pip install -r requirements.txt
```

### Do not activate the venv

This is the single most important operational rule in this file.

`source .venv/bin/activate` is **not** part of any workflow here. Agent sessions
and CI steps get fresh non-interactive shells, so an activation performed in one
step does not survive into the next. The resulting failures are `ModuleNotFound`
and wrong-interpreter errors deep inside a build step, which look exactly like
toolchain bugs and get debugged as such.

Instead, the Makefile defines:

```make
PYTHON ?= .venv/bin/python3
```

and every Python invocation in the Makefile goes through `$(PYTHON)`.

### The PATH shim, and why it exists

`$(PYTHON)` only covers Python that the *Makefile* launches. Two upstream shell
scripts call a bare `python3` themselves:

- `scripts/extract_baserom.u.sh:83`
- `scripts/clean_baserom.sh:33`

The first of these runs during `extractassets`, which is inside the matching
build path. Editing those scripts was not an option (Session 0 does not modify
decomp source), so the Makefile instead puts the venv's `bin` on the front of
`PATH` for the whole process tree:

```make
SL_VENV_BIN := $(abspath $(dir $(PYTHON)))
ifneq ($(wildcard $(SL_VENV_BIN)/activate),)
  export PATH := $(SL_VENV_BIN):$(PATH)
endif
```

A bare `python3` in any child process therefore resolves to the venv, with no
upstream edits and no activation. The `wildcard` guard means that overriding
`make PYTHON=python3` cannot accidentally put the repo root on `PATH`.

Verify the whole thing works in a shell that has never seen the venv:

```bash
env -i PATH=/usr/bin:/bin HOME=/tmp /usr/bin/make -C /path/to/sightline check-layering
```

### What is pinned, and what is deliberately absent

See `requirements.txt` for the pins. Three tools commonly assumed to be part of
a decomp environment are **not** installed, because this repo does not use them:

- **splat** — this decomp has its own extractor (`tools/extractor`, driven by
  `scripts/filelist.*.csv`). Nothing references splat.
- **capstone Python bindings** — superseded by the vendored rabbitizer.
- **the permuter** — not vendored, not referenced by any build path.

Add them if and when something actually imports them. The matching build itself
needs **no** third-party Python at all: the four scripts the Makefile invokes
import only the standard library. Everything in `requirements.txt` exists for
`tools/diff.py` (the vendored asm-differ), used via `scripts/asmdiff.sh`.

---

## 4. The base ROM

Sightline ships no game assets. Supply your own dump of the **USA** cartridge at
the repo root as `baserom.u.z64`.

It is gitignored twice over and must never be committed.

Verify it before building anything:

```bash
make verify-rom
```

Expected for the US ROM:

```
size    12582912 bytes (12 MiB)
magic   0x80371240        (z64, big-endian)
region  country code 'E'  (USA / North America)
sha1    abe01e4aeb033b6c0836819f549c791b26cfde83
```

`make matching` runs this check first and refuses to build on failure.

### Why this gate exists

A byte-swapped dump contains *correct data in the wrong byte order*. It has a
completely different SHA-1, and if you skip straight to hashing, all you learn is
"hash mismatch" — which sends people to re-dump a cartridge that was fine, or to
suspect the compiler. So the check tests size, then magic, then region, then
SHA-1, and names the actual cause:

| Magic | Meaning | Fix |
|---|---|---|
| `0x80371240` | `.z64`, big-endian | correct |
| `0x37804012` | `.v64`, 16-bit byte-swapped | `dd conv=swab`, or `ucon64 --z64` |
| `0x40123780` | `.n64`, 32-bit word-swapped | `ucon64 --z64` (`conv=swab` is *not* enough) |

Region is read from the country code at offset `0x3E`: `E` = USA, `P` = Europe,
`J` = Japan. A wrong-region ROM otherwise builds for a long time before failing.

---

## 5. Building from scratch

```bash
git clone https://gitlab.com/kholdfuzion/goldeneye_src.git sightline
cd sightline
git remote rename origin upstream        # upstream = the decomp; origin = Sightline

cp /path/to/your/baserom.u.z64 .
make verify-rom                          # fail here, not 20 minutes in

make venv                                # Python tooling
make matching -j$(nproc)                 # builds tools, extracts assets, builds ROM
```

`make matching` on 24 cores takes a few minutes warm; a cold build including
asset extraction and the IDO recompiler takes longer.

Success looks like:

```
    MATCH!

Rom File Generated in Build Directory.
```

The output is `build/u/ge007.u.z64`. With `COMPARE=1` (the default) the build
verifies its own SHA-1 against `ge007.u.sha1`. For a correct decomp the built ROM
is bit-for-bit identical to the base ROM, so this also holds:

```bash
cmp build/u/ge007.u.z64 baserom.u.z64
```

### The IDO compiler

The ROM is compiled by IDO 5.3, SGI's IRIX compiler, which is what Rare used.
Modern gcc cannot reproduce its code generation, so matching requires IDO itself.

Two routes exist. Sightline uses the first:

- **`ido-static-recomp` (default, `IDO_RECOMP := YES`)** — the IRIX compiler
  binaries statically recompiled to native Linux executables. Source is vendored
  at `tools/ido5.3_recomp/`, pinned to `decompals/ido-static-recomp @ d5aec59`.
  Built automatically by `make matching` via `scripts/make/build_tools.sh`;
  produces `tools/ido5.3_recomp/cc` and friends. No emulation, fast, no extra
  packages.
- **`qemu-irix` (`IDO_RECOMP=NO`)** — runs the original IRIX binaries under an
  emulator, requiring a `qemu-irix` package. Slower and an extra system
  dependency. Not used here.

The recompiled compilers are gitignored build products. `tools/irix/root/` holds
the IRIX binaries they are recompiled *from*; these come from upstream.

Note that `cc` does not understand `--version`; `tools/ido5.3_recomp/cc -version`
returning `malformed or unknown option` means the binary is present and running.

---

## 6. Known sharp edges

### `make matching` is deliberately two-phase

Asset extraction **writes source files** (`assets/music/*.bin` among others) that
the main dependency graph then globs. Under `-j`, make evaluates the ROM's
prerequisites before extraction has produced them, and fails with:

```
make: *** No rule to make target 'build/u/assets/music/Marchives.rz',
      needed by 'build/u/ge007.u.elf'.  Stop.
```

That message reads as a broken Makefile rule. It is not — it is a race, and it
only appears on a cold tree, which is why it is easy to misdiagnose after a
successful warm build. The `matching` target therefore runs extraction to
completion first, then builds:

```make
matching: verify-rom
	@$(MAKE) --no-print-directory all_p1 VERSION=$(VERSION)
	@$(MAKE) --no-print-directory all VERSION=$(VERSION) COMPARE=1
```

Upstream's `make -j` and `allbuild.sh` are both exposed to this; `make matching`
is not. If you invoke `make all -j` directly on a clean tree and see the error
above, this is what you hit — rerunning the command "fixes" it, because
extraction completed during the failed run.

### Filesystem

Keep the repo on a real Linux filesystem. Under WSL2 this means a path like
`/mnt/projects/...` backed by ext4 — **never** `/mnt/c` or `/mnt/d`. DrvFs is
case-insensitive and mangles permission bits, which breaks generated symbol files
and script executability in ways that surface much later and look unrelated.

### Build warnings that are expected

The build is not warning-free and is not meant to be. These are normal:

- `as1: Warning: ... number outside range for single precision floating point values`
- `Warning: end of file not at end of a line; newline inserted`
- `libc_impl.c: warning: the use of 'tmpnam'/'mktemp'/'tempnam' is dangerous`
  (from building the IDO recompiler)

They come from faithfully-preserved original source and from the recompiler's
libc shim. Do not "fix" them — see `docs/project-rules.md`, non-negotiable 5.

---

## 7. Trace harness emulator: libretro, not mupen64plus

The harness runs the ROM under **parallel_n64 via libretro**, driven by our own
frontend (`tools/trace/sltrace/emu_libretro.py`).

### Why not mupen64plus

mupen64plus is intermittently nondeterministic under this workload: roughly one
replay in four reproduced, with the divergence point wandering between ticks 789
and 1416 depending on configuration. Eliminated by measurement, in order: the
sampler, the state schema, `RandomizeInterrupt`, `CountPerOp`/`SiDmaDuration`
pinning, three renderers, both RSP plugins, the dynamic recompiler (confirmed off
by config readback), and the speed limiter. See `docs/backlog.md` B-005.

parallel_n64 reproduces byte-identically with no tuning at all: five consecutive
2136-tick runs through the full pipeline, all identical.

The structural reason is `retro_run()`, which advances **exactly one frame**.
There is no sampling point to choose, which removes an entire class of bug -
VI-boundary jitter, breakpoint plumbing, boot-phase filtering. There is also no
plugin ecosystem whose timing can leak into the simulation, no speed limiter,
and no dynarec.

### Getting the core

    mkdir -p ~/.config/retroarch/cores && cd ~/.config/retroarch/cores
    curl -sSLO https://buildbot.libretro.com/nightly/linux/x86_64/latest/parallel_n64_libretro.so.zip
    unzip -o parallel_n64_libretro.so.zip && rm parallel_n64_libretro.so.zip

On Windows, from PowerShell:

    $dir = "$env:LOCALAPPDATA\sightline\libretro"
    New-Item -ItemType Directory -Force $dir | Out-Null
    $url = 'https://buildbot.libretro.com/nightly/windows/x86_64/latest/parallel_n64_libretro.dll.zip'
    Invoke-WebRequest $url -OutFile "$dir\core.zip"
    Expand-Archive "$dir\core.zip" -DestinationPath $dir -Force
    Remove-Item "$dir\core.zip"

Or use RetroArch's own Online Updater -> Core Downloader -> ParaLLEl N64.
Override the path with `SL_LIBRETRO_CORE` if it lives elsewhere.

The core is an **external dependency and is never committed** - see
`docs/project-rules.md`, non-negotiable 2. `emu_libretro.py` only ever *discovers* one, and derives its
default from `Path.home()`/`%LOCALAPPDATA%` at runtime so no absolute home
directory is written down.

**Match the core's architecture to your Python.** A win64 DLL needs 64-bit
Python; ctypes reports a mismatch as "not a valid Win32 application", which
reads like a corrupt download rather than a wrong build.

`mupen64plus_next` does not load under a minimal frontend and is not used.

### The backend is portable; only its paths were not

Measured 2026-09-03: booting the retail ROM from ordinary Windows Python needed
**no change to the emulator code at all**. `ctypes.CDLL` loads the `.dll`, the
callbacks bind, RDRAM reads work and results are byte-identical to the Linux
side. The only Windows blockers were two hardcoded Linux paths - the core's
default location and `/tmp/sl-libretro` - both now chosen per platform.

### Facts the backend depends on, all measured

| Fact | Consequence |
|---|---|
| RAM pointer is NULL until after the first `retro_run()` | fetch it after one frame, never at load |
| `retro_serialize_size`/`retro_unserialize` also fail before the first frame | savestate restore needs a warm-up frame |
| `SYSTEM_RAM` holds 32-bit words in host order | every read swaps, same as mupen64plus |
| A button HELD from frame zero has no effect | menus respond to press EDGES; 21744 held presses hashed identically to no input |
| The core defaults `parallel-n64-pak1..4` to `none` | pinned to `memory`: GoldenEye probes the controller pak, and a mismatch diverged at tick 1 under mupen64plus |
| Per-frame port-0 query order is fixed | 16 joypad ids, then analog RIGHT X/Y, then LEFT X/Y |
| `dlopen` returns the SAME core for an already-loaded path | ONE emulator per process; a second one resumes the first's state instead of booting |

### Recording

Recording runs inside the harness (`record_libretro.py`) rather than through
RetroArch. RetroArch's replay format is positional - it logs the return of every
`retro_input_state` call in order - so replaying it needs our frontend to make a
byte-identical query sequence to RetroArch's, including whatever it polls for
itself and whichever side of its input remap it logs. Replaying a real recording
moved the game but never reproduced the session. Owning both ends removes the
coupling.

Video is XRGB8888 at 640x480 blitted to SDL; input comes from SDL, which sees
the pad where RetroArch's default `udev` driver cannot, because WSL runs no udev
daemon.

### The round trip is the property that matters

Replay determinism alone does not make a harness trustworthy. If the logged
stream is applied one frame early, every replay reproduces the same *wrong* run
perfectly, and a five-runs-agree check still passes. What has to hold is that
what the recorder writes is what replay plays back:

```
make trace-roundtrip [FRAMES=600]
```

It records with a scripted pad (pulsed START - neutral input would sit on one
screen, where a misapplied stream looks identical to a correct one), replays the
resulting file, and compares both the per-tick state hash and the frame-counter
progression. On failure it says which of the two things went wrong, because they
need different fixes: counters differing means the record and replay loops are
misaligned, while counters agreeing and state differing means the runs genuinely
behaved differently.

Verified at 600 frames (`66d5ea64...`) and 2400 (`c35875bd...`), so this is
not a short-window result that drift would escape. CI runs the 600-frame
form on every push that touches `tools/trace/`.

Each half runs as a subprocess. That is required, not tidiness - see the dlopen
row above. `LibretroEmulator` raises `SecondCoreError` if a process tries to
build a second one.

### Booting straight into a stage

Recording twenty-plus levels through the menus, then replaying that walk on
every gate run, is the dominant cost of Phase 0. `make direct-boot LEVEL=x`
builds a ROM that starts in the mission on Agent instead.

It is not a new feature. `bossMainloop` already validates saves, selects folder
1, sets Agent difficulty, prepares the briefing and calls `lvlStageLoad` when
`g_StageNum` is not the title. Only setting the variable was missing.

Setting the **initialiser** is what makes it reachable, and the timing is the
whole reason:

| Approach | Result |
|---|---|
| Write `g_StageNum` from the harness at frame 2 | write sticks, changes nothing - the boot check has already run, and RDRAM is unreachable before frame 1 |
| Rare's `-level_NN` switch | `tokenReadIo` reads it from cart `0xFFB000`, past the end of a retail image; and `g_Tokens` ends up holding the game's built-in `-m*` pool defaults regardless. Padding a ROM to `0xFFB000` and writing the switch there was measured to do nothing |
| `SL_DIRECT_BOOT_LEVEL` initialiser | works; value is in place before the first instruction, so there is no window to hit and nothing written at runtime |

Each level needs its own build: the segment holding the initialiser is
compressed, so changing one word moved 58103 bytes across a 209KB span. There
is no byte to patch in a finished image. Builds take under a minute and are
cached in `build/u/direct/`.

`direct-boot` always rebuilds the matching ROM afterwards. `build/u/ge007.u.z64`
is what the harness reads by default, and leaving a direct-boot image there
would quietly record every later trace against the wrong ROM. Replay picks its
image by matching the hash the trace was recorded with, so levels captured
before direct-boot existed keep verifying without special-casing.

### The cartridge save

The core exposes one opaque 296960-byte blob covering SRAM, FlashRAM, EEPROM
and four controller paks. The harness treats it as opaque and copies it whole,
so nothing depends on which of those this cartridge uses.

Two rules, both learned the hard way:

- **Replay never writes it back.** The game saves as you play, so a replay that
  stored its result overwrote the snapshot it was replaying - run 1 passed and
  every run after started from different progress.
- **A recording snapshots the save it started from**, written after the core
  loads it. Taken any earlier it captures a blank cartridge. Both failures
  presented as a nondeterministic game.

`make trace-unlock` seeds the save so every stage is selectable, by filling the
time table - a stage counts as completed when its stored time is non-zero, so
the packed 10-bit fields never need decoding. Save slots are found by
recomputing their checksums and demanding a match against what the game wrote,
which is also how the save area was located inside the blob.

## 8. Trace harness environment (historical: mupen64plus)

The harness needs a real gfx plugin for the game to advance at all, so it needs
a display; under CI that is Xvfb. Hashes are identical with Xvfb and with a real
display on the same machine.

They are **not** identical across machines or between host and container, even
with identical package versions. See `tools/trace/README.md`. The consequence for
reproduction: produce and verify traces in the canonical container via
`ci/image/run.sh`, built from `ci/image/Dockerfile`.

    docker build -t sightline-ci:24.04 -f ci/image/Dockerfile ci/image
    ci/image/run.sh capture --out /workspace/traces/facility.sltrace --level facility

The image contains no ROM and no assets; the base ROM is bind-mounted read-only
from the machine that owns it.

## 9. Layering check

```bash
make check-layering             # report + fail on NEW violations
make check-layering-baseline    # regenerate baseline (only when it shrinks)
```

`src/game/` must not include from `src/gfx/`, `src/platform/`, or the
libultra/RCP layers. This is not true today — severing it is Phase 1's work — so
the check runs in reporting mode against `docs/layering-baseline.txt` and fails
only on violations *not* already in the baseline. The remaining count prints on
every run.

Baseline at Session 0: **282 violations across 235 files**, all `src/game/` →
libultra/RCP headers (`ultra64.h`, `PR/*`, `gbi_extension.h`). This number must
decrease monotonically and reach zero at the Phase 1 gate. Never add a line to
the baseline to make a build pass.

Implementation: `tools/sightline/check_layering.py`.
