<#
.SYNOPSIS
    Build and run the offline tests for the opt-in GL debug-output diagnostic
    (src/gfx/sl_gfx_gldebug.c).

.DESCRIPTION
    Run from an ordinary Windows PowerShell prompt at the repository root:

        .\tools\windows\gldebugtest.ps1

    WHAT IT RUNS. tools/profiling/native/test_sl_gfx_gldebug.c, which #includes
    the module's own text and drives it against a recording mock of the eight
    platform calls the module takes by pointer. Nine modes, each its own
    process because the module's armed flag, its bound-callback state and its
    per-process message cap all have process lifetime by construction:

        parse      the selector's grammar, including 24 malformed forms
        arm        the pre-creation request: its silence when unset, a
                   malformed value refused without being echoed, and a refusal
                   on either SDL failure - an unreadable attribute (no set call
                   is made at all, so nothing unseen can be clobbered) or a
                   failed set, including a failed set that stored anyway and
                   whose original value is put back and proved
        grant      REQUESTED versus GRANTED. The context's own GL_CONTEXT_FLAGS
                   is the ONLY source; SDL's attribute is advisory, because it
                   stores the REQUEST and accepting it would make the check
                   confirm what this module had just asked for
        lookup     a NULL entry point, and failing closed
        callback   the bound pointer, no platform call from inside it, the cap,
                   and the install sequence en,en,cbset,ise,ise
        enable     six ways glEnable fails to take effect - a GL error, a
                   silent no-op, an untrustworthy readback, each on either
                   capability, and a post-bind regression - each refusing,
                   clearing the callback before disabling, ending inactive
        lifecycle  clear-then-disable, idempotence, no re-arm
        default    the path a player launch takes: nothing happens at all
        format     the message formatter's four independent bounds

    NO WINDOW, NO CONTEXT, NO DRIVER, NO GPU, AND NO LINK AGAINST EITHER
    LIBRARY. The real <GL/gl.h>, <GL/glext.h> and <SDL2/SDL_video.h> are
    included for their TYPES AND VALUES - so the module's transcribed callback
    types are checked against Khronos's GLDEBUGPROC and
    PFNGLDEBUGMESSAGECALLBACKPROC by no-cast assignment, and every asserted
    token comes from the registry header rather than from a second
    transcription - but no function from either library is referenced.
    -D_GDI32_ makes mingw's WINGDIAPI empty (mingw32/include/wingdi.h:13-16) so
    gl.h declares ordinary symbols rather than imports from opengl32, the same
    device tools/windows/bgtfseamtest.ps1 uses; the link therefore needs
    neither -lopengl32 nor -lSDL2, and a build that started needing one would
    mean the module had grown a direct platform call.

    THE FLAGS ARE STRICTER THAN THE PRODUCT'S ON PURPOSE. src/gfx compiles with
    -w in tools/windows/build.ps1, which silences the one diagnostic that makes
    the callback's CALLING CONVENTION checkable: on Win32 the callback must be
    __stdcall, and a __cdecl one would corrupt the stack on return with nothing
    at run time saying so clearly. This script compiles the same text with
    -Wall -Wextra -Werror -Werror=incompatible-pointer-types, so the fixture's
    four no-cast assignments between the module's types and the registry's are
    hard errors when they disagree. That is the whole mechanism behind the
    compile-time half of this suite.

    WHAT THIS SUITE DOES NOT ESTABLISH. Anything about a real driver: whether
    one grants a debug context, resolves the entry point, or reports anything.
    And nothing about the renderer - a diagnostic that reports nothing
    exonerates no code, and the selector changes the driver's own behaviour, so
    a run under it is not the run being asked about.

    Exits 0 when every mode passes, 1 otherwise.

.PARAMETER Msys2Root
    Where MSYS2 lives, if it is not in one of the usual places. The
    SL_MSYS2_ROOT environment variable does the same thing.

.PARAMETER OutDir
    Where the object and the executable go. Defaults to build\win32 beside the
    other fixtures; point it elsewhere to keep a run out of the build tree.
#>
[CmdletBinding()]
param([string]$Msys2Root, [string]$OutDir)

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if ($OutDir) { $OUT = $OutDir } else { $OUT = Join-Path $repo 'build\win32' }

if (-not $Msys2Root) { $Msys2Root = $env:SL_MSYS2_ROOT }
$roots = @()
if ($Msys2Root) { $roots += $Msys2Root }
$roots += @('C:\msys64', 'C:\msys2', "$env:SystemDrive\msys64")
$mingwBin = $null
foreach ($r in $roots) {
    $cand = Join-Path $r 'mingw32\bin'
    if (Test-Path (Join-Path $cand 'i686-w64-mingw32-gcc.exe')) { $mingwBin = $cand; break }
}
if ($null -eq $mingwBin) {
    Write-Error "gldebugtest.ps1: no mingw32 toolchain found. Run tools\windows\setup.ps1."
}
$CC = Join-Path $mingwBin 'i686-w64-mingw32-gcc.exe'

New-Item -ItemType Directory -Force -Path $OUT | Out-Null
$exe = Join-Path $OUT 'gldebugtest.exe'

# -m32 like the rest of the native build (the decomp assumes 32-bit pointers),
# and the calling convention under test is the 32-bit one - on x86-64 __stdcall
# is ignored, so a 64-bit build of this fixture would prove nothing about it.
$flags = @('-m32', '-O2', '-Wall', '-Wextra', '-Werror',
           '-Werror=incompatible-pointer-types',
           '-Werror=implicit-function-declaration',
           '-D_GDI32_',
           '-I' + (Join-Path $repo 'src\gfx'))
$sources = @('tools/profiling/native/test_sl_gfx_gldebug.c')

$modes = @('parse', 'arm', 'grant', 'lookup', 'callback', 'enable',
           'lifecycle', 'default', 'format')

$rc = 0
$savedPath = $env:PATH
$savedCwd  = (Get-Location).Path
try {
    # cc1 and the runtime DLLs live beside gcc; without this the driver fails
    # with a missing-DLL error that reads like a missing compiler.
    $env:PATH = "$mingwBin;$env:PATH"
    Set-Location $repo
    & $CC @($flags + @('-o', $exe) + $sources)
    if ($LASTEXITCODE -ne 0) { throw "gldebugtest.ps1: compile failed (exit $LASTEXITCODE)" }

    foreach ($mode in $modes) {
        & $exe $mode
        if ($LASTEXITCODE -ne 0) {
            Write-Host "FAIL: mode $mode exited $LASTEXITCODE"
            $rc = 1
        }
    }
} finally {
    Set-Location $savedCwd
    $env:PATH = $savedPath
}
if ($rc -eq 0) { Write-Host 'gldebugtest: all modes passed' }
exit $rc
