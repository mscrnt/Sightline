<#
.SYNOPSIS
    Build and run the model-file swap self-test.

.DESCRIPTION
    Run from an ordinary Windows PowerShell prompt at the repository root:

        .\tools\windows\modeltest.ps1

    src/native/sl_swap_model.c compiled with -DSL_SWAP_SELFTEST, under the
    game translation units' own flags, linked with src/platform/sl_win32_rt.c
    for its stderr. It checks the collision-point swap against the two rows
    of "Point Merging.txt"'s Collision Point Table, given in wire order -
    coordinates, the point's id, the link offset and the related point's
    number (0x000D, not the byte-reversed 0x0D00) - and the merged-point link
    check (sl_model_links_check) on a synthetic two-piece file: in range,
    byte-reversed, one past the end, no link.

    NO ROM AND NO GAME is involved; the fixtures are bytes in the test.

    Exits 0 when every check passes, 1 otherwise. Same toolchain ladder as
    build.ps1.

.PARAMETER Msys2Root
    Where MSYS2 lives, if it is not in one of the usual places. The
    SL_MSYS2_ROOT environment variable does the same thing.
#>
[CmdletBinding()]
param([string]$Msys2Root)

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$OUT  = Join-Path $repo 'build\win32'

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
    Write-Error "modeltest.ps1: no mingw32 toolchain found. Run tools\windows\setup.ps1."
}
$CC = Join-Path $mingwBin 'i686-w64-mingw32-gcc.exe'

New-Item -ItemType Directory -Force -Path $OUT | Out-Null
$exe = Join-Path $OUT 'modeltest.exe'
$swapObj = Join-Path $OUT 'modeltest_swap.o'
$rtObj = Join-Path $OUT 'modeltest_rt.o'

$rc = 0
$savedPath = $env:PATH
$savedCwd  = (Get-Location).Path
try {
    $env:PATH = "$mingwBin;$env:PATH"
    Set-Location $repo
    # the game translation units' flags (build.ps1), so the structs are laid
    # out as the game's are
    & $CC @('-m32', '-g', '-mno-ms-bitfields', '-std=gnu89', '-fms-extensions', '-fno-builtin',
            '-fno-common', '-w', '-DVERSION_US', '-DLANG_US', '-DREFRESH_NTSC', '-DTARGET_N64',
            '-D_LANGUAGE_C', '-DSL_SWAP_SELFTEST', '-I.', '-Iinclude', '-Iinclude/PR', '-Isrc',
            '-Isrc/game', '-Isrc/libultra', '-c', 'src/native/sl_swap_model.c', '-o', $swapObj)
    if ($LASTEXITCODE -ne 0) { throw "modeltest.ps1: compile failed (exit $LASTEXITCODE)" }
    & $CC @('-m32', '-g', '-c', 'src/platform/sl_win32_rt.c', '-o', $rtObj)
    if ($LASTEXITCODE -ne 0) { throw "modeltest.ps1: runtime compile failed (exit $LASTEXITCODE)" }
    & $CC @('-m32', '-o', $exe, $swapObj, $rtObj)
    if ($LASTEXITCODE -ne 0) { throw "modeltest.ps1: link failed (exit $LASTEXITCODE)" }
    & $exe
    if ($LASTEXITCODE -ne 0) { $rc = 1 }
} finally {
    Set-Location $savedCwd
    $env:PATH = $savedPath
}
exit $rc
