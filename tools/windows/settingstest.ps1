<#
.SYNOPSIS
    Build and run the native settings store self-test (#41).

.DESCRIPTION
    Run from an ordinary Windows PowerShell prompt at the repository root:

        .\tools\windows\settingstest.ps1

    Compiles src/platform/sl_settings.c ALONE with -DSL_SETTINGS_SELFTEST and
    runs the fixture inside it: the parser against a hostile file (comments,
    CRLF, spaces, unknown keys, malformed and out-of-range values, a newer
    version), the writer (parent directories, atomic replace, no .tmp left
    behind, write-only-on-change), a reload, and the one-time .style import.
    Every fixture is synthesised in code and lands under %TEMP% through the
    SL_CONFIG override; the player's own config is never opened.

    Exits 0 when every check passes, 1 otherwise. Same toolchain ladder as
    build.ps1 / inputtest.ps1.

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
    Write-Error "settingstest.ps1: no mingw32 toolchain found. Run tools\windows\setup.ps1."
}
$CC = Join-Path $mingwBin 'i686-w64-mingw32-gcc.exe'

New-Item -ItemType Directory -Force -Path $OUT | Out-Null
$exe = Join-Path $OUT 'settingstest.exe'

$savedPath = $env:PATH
$savedCwd  = (Get-Location).Path
try {
    $env:PATH = "$mingwBin;$env:PATH"
    Set-Location $repo
    & $CC @('-m32', '-O0', '-g', '-Wall', '-Wextra', '-DSL_SETTINGS_SELFTEST',
            '-o', $exe, 'src/platform/sl_settings.c')
    if ($LASTEXITCODE -ne 0) { throw "settingstest.ps1: compile failed (exit $LASTEXITCODE)" }
    # The store's own diagnostics go to stderr, and PowerShell 5.1 turns a
    # native process's stderr into error records under -ErrorAction Stop.
    # cmd owns the redirect instead; the verdict line is on stdout.
    & cmd /c "`"$exe`" 2>nul"
    $rc = $LASTEXITCODE
} finally {
    Set-Location $savedCwd
    $env:PATH = $savedPath
}
exit $rc
