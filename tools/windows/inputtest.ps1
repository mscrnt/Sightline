<#
.SYNOPSIS
    Build and run the headless keyboard / mouse / pointer translation test.

.DESCRIPTION
    Run from an ordinary Windows PowerShell prompt at the repository root:

        .\tools\windows\inputtest.ps1

    The Windows counterpart of tools/native/inputtest.sh, and the same idea:
    it links the REAL src/platform/sl_input.c against the stub SDL and stub
    front-end queries in tools/native/inputtest.c, so what is asserted is the
    shipping translation and not a second copy of it. No window, no ROM, no
    pointer, no compositor - which is the point, because every attempt to drive
    this path with a real pointer has produced contradictory readings.

    IT EXITS 1 ON A CLEAN TREE TODAY, and that is a finding rather than a
    breakage in this script. tools/native/inputtest.c was last updated at
    fbcc3a48; src/platform/sl_input.c has three commits after it, two of which
    changed behaviour the harness pins - the per-platform mouse sign default
    (7936cbd0, B-071) and the removal of the mouse-becomes-the-stick aim
    routing (911603cc). 14 of 80 checks fail for those two reasons, all of them
    in the gameplay mouse-look sections. Making them match the binary's current
    output would be a re-baseline of an owner-validated hand measurement, which
    project rule 1 says is its own commit touching nothing else.
    Recorded as B-096 in docs/backlog.md.

    Having no Windows runner is exactly why that rot went unnoticed, which is
    the case for this script existing rather than against it.

.PARAMETER Msys2Root
    Where MSYS2 lives, if it is not in one of the usual places. The
    SL_MSYS2_ROOT environment variable does the same thing. Same resolution
    order as build.ps1.
#>
[CmdletBinding()]
param([string]$Msys2Root)

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$OUT  = Join-Path $repo 'build\win32'

# Same toolchain ladder as build.ps1: MSYS2 is a TOOLCHAIN PROVIDER only, and
# PATH is set for this script's children and restored on the way out.
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
    Write-Error "inputtest.ps1: no mingw32 toolchain found. Run tools\windows\setup.ps1."
}
$CC = Join-Path $mingwBin 'i686-w64-mingw32-gcc.exe'

# SDL2 headers only - the harness DEFINES the SDL entry points it needs, so
# nothing links against the real library. -Dmain=SDL_main is stripped for the
# same reason build.ps1 strips it: it would rewrite this file's own main.
if ($env:SL_SDL_CFLAGS) {
    $sdlRaw = $env:SL_SDL_CFLAGS
} else {
    $pkgconf = $null
    foreach ($nm in @('pkgconf.exe', 'pkg-config.exe')) {
        $cand = Join-Path $mingwBin $nm
        if (Test-Path $cand) { $pkgconf = $cand; break }
    }
    if ($null -eq $pkgconf) {
        Write-Error "inputtest.ps1: no pkgconf/pkg-config in $mingwBin. Run setup.ps1."
    }
    $sdlRaw = (& $pkgconf --cflags sdl2) -join ' '
    if ($LASTEXITCODE -ne 0 -or -not $sdlRaw) {
        Write-Error "inputtest.ps1: pkgconf could not describe sdl2. Run setup.ps1."
    }
}
$SDL_CFLAGS = @($sdlRaw -split '\s+' |
                Where-Object { $_ -ne '' -and $_ -ne '-Dmain=SDL_main' })
$SDL_CFLAGS += '-DSDL_MAIN_HANDLED'

New-Item -ItemType Directory -Force -Path $OUT | Out-Null
$exe = Join-Path $OUT 'inputtest.exe'

$savedPath = $env:PATH
$savedCwd  = (Get-Location).Path
try {
    $env:PATH = "$mingwBin;$env:PATH"
    Set-Location $repo
    & $CC (@('-m32', '-O0', '-g', '-Wall', '-Wno-unused-parameter',
             '-mno-ms-bitfields', '-o', $exe,
             'tools/native/inputtest.c', 'src/platform/sl_input.c') + $SDL_CFLAGS)
    if ($LASTEXITCODE -ne 0) { throw "inputtest.ps1: compile failed (exit $LASTEXITCODE)" }
    & $exe
    $rc = $LASTEXITCODE
} finally {
    Set-Location $savedCwd
    $env:PATH = $savedPath
}
exit $rc
