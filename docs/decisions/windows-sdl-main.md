# Windows: keep our own `main`, decline SDL2's WinMain shim

**Decided:** 2026-09-01, during Win32/i686/PE32 bring-up.
**Status:** in force for the Win32 build. No effect on Linux or the matching build.

## The decision

The Win32 build keeps the plain `int main(void)` at `src/platform/sl_main.c`,
declares `SDL_MAIN_HANDLED`, calls `SDL_SetMainReady()` at the top of
`sdl_init()`, links **without** `-lSDL2main`, and builds for the **console**
subsystem (`-mconsole`).

Concretely the build strips `-Dmain=SDL_main` out of what `pkgconf --cflags
sdl2` emits, and drops `-lSDL2main` and `-mwindows` from the link.

## Why not SDL2's normal arrangement

SDL2 on Windows expects to own program entry: `SDL_main.h` defines the token
`main` to `SDL_main`, and `libSDL2main` supplies the real `WinMain` that sets up
argv and then calls yours. Three things make that wrong here.

**1. `-Dmain=SDL_main` is a preprocessor rewrite applied to every translation
unit it reaches, not just to the one holding entry.** This tree has two
`int main(void)` definitions inside the build glob — `src/platform/sl_main.c`
and `src/gfx/sl_gfx_tex.c` (the latter behind `SL_TEX_SELFTEST`). Measured, the
rewrite is a hard error, not a warning:

```
error: conflicting types for 'SDL_main'; have 'int(void)'
note: previous declaration of 'SDL_main' with type 'int(int, char **)'
```

`SDL_MAIN_HANDLED` in the source cannot prevent this, because the define
arrives on the command line rather than through the header.

**2. `-mwindows` would destroy the project's evidence base.** The GUI subsystem
detaches the console, so `stdout` and `stderr` go nowhere. Every diagnostic
this port was built on — the boot log, the pager and heap reports, the crash
and hang reports, the audio arena warning, `survived N pumped frames` — is
written to `stderr`. A build that discards them would be undebuggable by
exactly the method this project relies on. The PE header records
`Subsystem: 3 (CONSOLE)`; that is deliberate and should stay.

**3. We do not need what `SDL_main` provides.** Its job is argv marshalling and
main-thread setup. `sl_main.c` takes no arguments — it is configured entirely
through `SL_*` environment variables — so there is nothing to marshal.

## What this obliges

`SDL_SetMainReady()` must be called before `SDL_Init`, or SDL refuses to
initialise on Windows. It lives at the top of `sdl_init()` in
`src/gfx/sl_gfx_sdl.c` guarded by `#ifdef SDL_MAIN_HANDLED`, so it is inert on
Linux, which does not define it.

## Verified

A hidden-window probe built the same way (32-bit PE, `SDL_MAIN_HANDLED`, no
`SDL2main`) reached: `SDL_Init(VIDEO)` OK, video driver `windows`, GL context
created, `GL_VERSION 4.6.0`, clean teardown — with nothing drawn on screen.
