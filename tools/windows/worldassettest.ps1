<#
.SYNOPSIS
    Build and run the world-asset provider self-tests (#68).

.DESCRIPTION
    Run from an ordinary Windows PowerShell prompt at the repository root:

        .\tools\windows\worldassettest.ps1

    Two suites, both on SYNTHETIC fixtures only:

      RUNTIME   src/gfx/sl_gfx_worldasset.c compiled with -DSL_WA_SELFTEST.
                The SLWA parser against every rejection it has - magic,
                version, asset class, item path (case, separators, empty,
                unterminated, junk past the terminator), reserved fields,
                counts that are zero, past their limit or would overflow
                their own byte length, section offsets that are unaligned,
                outside the file, inside the header or overlapping another
                section, a declared size that is not the file's length, and
                bad materials, images and mesh ranges - then the loader
                against a truncated file, a missing file, a file naming
                another item and a malformed one, and the provider policy:
                which set the PRESENTATION selects (ORIGINAL and COMMUNITY HD
                none, XBLA the xbla set), that the answer is re-asked rather
                than latched so F5 switches live, that the diagnostic
                SL_WORLD_ASSETS override pins the provider either way ahead of
                the presentation, one open per (class, path), a cached second
                lookup, an unknown class that never touches the filesystem;
                and (#70) the version-4 skinned item: its joints and skin,
                every refusal of the skin section, the joint table and the
                built-in-head mark, and that only PRESENTATION XBLA consumes a
                character or head item.

      SKIN      src/gfx/sl_gfx_charskin.c compiled with -DSL_CHR_SELFTEST.
                Relating a replacement item to a game skeleton on a synthetic
                pair: the common scale, the quarter turn of a limb laid along
                an axis, a leaf keeping its parent's turn, the deformation of
                a rigid and a blended vertex, and every refusal.

      TOOLS     tools/worldasset/selftest.py. The SLWA writer's own limits,
                that its output parses, that it is deterministic and that
                rerunning replaces rather than appends; the half-float
                reader; and the mesh splitter on a blob laid out by hand.

    NO SOURCE, NO PREPARED ASSET AND NO ROM is opened by either suite - the
    fixtures are bytes the tests write themselves under %TEMP% - so this runs
    on CI with nothing installed, which is the point.

    Exits 0 when every check passes, 1 otherwise. Same toolchain ladder as
    build.ps1 / texprovtest.ps1.

.PARAMETER Msys2Root
    Where MSYS2 lives, if it is not in one of the usual places. The
    SL_MSYS2_ROOT environment variable does the same thing.

.PARAMETER RuntimeOnly
    Skip the Python tools suite (for a machine with no project venv).
#>
[CmdletBinding()]
param([string]$Msys2Root, [switch]$RuntimeOnly)

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
    Write-Error "worldassettest.ps1: no mingw32 toolchain found. Run tools\windows\setup.ps1."
}
$CC = Join-Path $mingwBin 'i686-w64-mingw32-gcc.exe'

New-Item -ItemType Directory -Force -Path $OUT | Out-Null
$exe = Join-Path $OUT 'worldassettest.exe'

$rc = 0
$savedPath = $env:PATH
$savedCwd  = (Get-Location).Path
try {
    $env:PATH = "$mingwBin;$env:PATH"
    Set-Location $repo
    # #68: the provider asks the PRESENTATION POLICY which set is selected, so
    # the policy and the store it reads are linked in. Neither defines a main
    # here (their own selftest defines are absent), exactly as texprovtest has
    # always linked the store beside the texture provider. The store stays
    # INACTIVE throughout - sets change memory only and no config is written.
    & $CC @('-m32', '-O0', '-g', '-Wall', '-Wextra', '-DSL_WA_SELFTEST',
            '-o', $exe, 'src/gfx/sl_gfx_worldasset.c',
            'src/platform/sl_presentation.c', 'src/platform/sl_settings.c')
    if ($LASTEXITCODE -ne 0) { throw "worldassettest.ps1: compile failed (exit $LASTEXITCODE)" }
    # The provider's own diagnostics (INVALID lines, the summary) go to
    # stderr; cmd owns the redirect (PowerShell 5.1 would turn them into
    # error records). The verdict line is on stdout.
    & cmd /c "`"$exe`" 2>nul"
    if ($LASTEXITCODE -ne 0) { $rc = $LASTEXITCODE }
    # #70: the skeleton relation and the deformation, linked with the
    # provider (whose parser the skinned item is read by) and the policy.
    $skin = Join-Path $OUT 'charskintest.exe'
    & $CC @('-m32', '-O0', '-g', '-Wall', '-Wextra', '-DSL_CHR_SELFTEST',
            '-o', $skin, 'src/gfx/sl_gfx_charskin.c', 'src/gfx/sl_gfx_worldasset.c',
            'src/platform/sl_presentation.c', 'src/platform/sl_settings.c')
    if ($LASTEXITCODE -ne 0) { throw "worldassettest.ps1: charskin compile failed (exit $LASTEXITCODE)" }
    & cmd /c "`"$skin`" 2>nul"
    if ($LASTEXITCODE -ne 0) { $rc = $LASTEXITCODE }
} finally {
    Set-Location $savedCwd
    $env:PATH = $savedPath
}

if (-not $RuntimeOnly) {
    $python = $env:SL_PYTHON
    if (-not $python) { $python = Join-Path $repo '.venv\Scripts\python.exe' }
    if (-not (Test-Path $python)) {
        $sys = Get-Command python.exe -ErrorAction SilentlyContinue
        if ($null -ne $sys) { $python = $sys.Source } else { $python = $null }
    }
    if ($null -eq $python) {
        Write-Host 'worldassettest: no Python; the tools suite was skipped.'
    } else {
        & $python (Join-Path $repo 'tools\worldasset\selftest.py')
        if ($LASTEXITCODE -ne 0) { $rc = $LASTEXITCODE }
    }
}

exit $rc
