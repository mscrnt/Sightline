# tools/windows

The Windows developer workflow. Three commands, from an ordinary Windows
PowerShell prompt at the repository root. No MSYS2 shell, no Git Bash, no WSL.

```powershell
.\tools\windows\setup.ps1     # check the toolchain (add -Install to fix it)
.\tools\windows\build.ps1     # build build\win32\sightline.exe
.\tools\windows\play.ps1      # play Facility in a window
```

and one gate:

```powershell
.\tools\windows\test.ps1      # headless Facility acceptance, exits nonzero on failure
```

`play.ps1` and `test.ps1` build first if the exe is missing, so `build.ps1` is
only needed on its own when you want to see the build.

## What you need

MSYS2, used purely as a **toolchain provider** — `i686-w64-mingw32-gcc` is a
native Windows compiler producing PE32/i386, and nothing here asks you to
operate an MSYS2 shell.

```powershell
winget install --id MSYS2.MSYS2
.\tools\windows\setup.ps1 -Install
```

`setup.ps1` probes for MSYS2 in the usual locations. If yours is elsewhere,
name it — `-Msys2Root D:\msys64`, or the `SL_MSYS2_ROOT` environment variable,
which `build.ps1` honours too. An explicitly named root is authoritative:
neither script will quietly fall back to a different installation.

Every check ends in a query against the actual executable, and the last one
compiles and links a real SDL2 program and reads its PE header back. "Toolchain
satisfied" therefore means the toolchain *did the thing*, not that some files
were on disk.

You also need your own GoldenEye (U) ROM at `baserom.u.z64`, or `SL_ROM`
pointing at it. Nothing ROM-derived ships in this repository.

## Environment overrides

| Variable | Effect |
|---|---|
| `SL_MSYS2_ROOT` | where MSYS2 lives |
| `SL_ROM` | ROM path (default `baserom.u.z64`) |
| `SL_PYTHON` | interpreter for the generation steps (default `.venv\Scripts\python.exe`) |
| `SL_LEVEL`, `SL_DIFFICULTY`, `SL_WINDOW_SIZE` | `play.ps1` defaults |
| `SL_SAVE` | save file (default `%LOCALAPPDATA%\sightline\eeprom.bin`) |
| `SL_CONFIG` | developer-only: the native settings file (default `%LOCALAPPDATA%\sightline\config.ini`; the -Demo core uses `...\sightline\demo\config.ini`) - lets a test run against a scratch config. Besides its scalars the file carries the player's gameplay bindings as `bind.<action>.<device>.<slot>=<token>` lines (only the slots that differ from the compiled defaults; `key:UP`, `mouse:X1`, `wheel:DOWN`, `pad:RT`, `none`), written by the BINDINGS editors - OPTIONS -> SETTINGS -> CONTROL -> BINDINGS, and the watch's SIGHTLINE -> BINDINGS - and never by hand while the game runs |
| `SL_TRACE_DEFS` | extra `-D` flags; changing it invalidates all objects |
| `SL_SDL_CFLAGS`, `SL_SDL_LIBS` | override what pkgconf reports |

The save file lives outside the repository on purpose: it is player data.

## Where things go

Objects and the executable land in `build\win32\`, which is gitignored, and
which is kept separate from `build\native\` because the link globs `*.o` and
would otherwise swallow the Linux ELF objects. `build.ps1` stages the non-system
runtime DLLs next to the exe — read out of the PE import table rather than
guessed — so a launch needs nothing on PATH.

`build.ps1` prepends the MSYS2 `mingw32\bin` directory to PATH for its own child
processes only, and restores it on the way out. So does `setup.ps1`. Your shell
is left as it was found.

## Why the build flags are what they are

`build.ps1` carries the reasoning inline, and none of those flags are tuning
knobs — each was measured against a failure that consumed a wrong value
silently. The short version, with the full write-ups in `docs/backlog.md`
(B-061) and `docs/decisions/windows-sdl-main.md`:

- **`-mno-ms-bitfields`** on both compile classes. MinGW defaults to the MSVC
  bitfield rule, which changed `StandTile` from 8 bytes to 12 and hung Facility
  forever in `stanDetermineEOF`.
- **`-fno-common`**. MinGW `ld` reordered `image.c`'s tentative globals and
  aliased `g_TexCacheCount` into the texture pool.
- **`-Wl,--large-address-aware`**. Without it the process tops out at
  `0x7ffeffff` and `sl_pager_init` cannot reserve the kseg2 window.
- **`-mconsole`, never `-mwindows`**. The GUI subsystem detaches the console and
  discards every `stderr` diagnostic this port was built on.
- **`-DSDL_MAIN_HANDLED`**, with `-Dmain=SDL_main` stripped from pkgconf's
  output and no `-lSDL2main`.

`docs/decisions/windows-workflow.md` records the workflow decision itself.

## Not here

`tools/native/` is the Linux side and is out of scope for this workflow; do not
cross-wire the two. `tools/windows/build.sh` was retired on 2026-09-02 when
`build.ps1` replaced it — its measured rationale is carried forward above and in
`build.ps1`'s own comments.
