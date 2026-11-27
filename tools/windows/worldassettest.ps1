<#
.SYNOPSIS
    Build and run the world-asset provider self-tests (#68).

.DESCRIPTION
    Run from an ordinary Windows PowerShell prompt at the repository root:

        .\tools\windows\worldassettest.ps1

    Four suites, on SYNTHETIC fixtures (one optional read named below):

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

      POOL      src/platform/sl_worker_pool.c compiled with -DSL_POOL_SELFTEST.
                The fill-query workers: physical against logical cores and
                constrained masks, the SL_FILL_WORKERS rule, random batches
                (sizes 0 to 4096, stealing, cancellation, floating-point
                environments, no write after a drain, stop and restart), a
                partial start, a worker delayed across a drain, and the
                shutdown witness (a stop that cannot prove its thread gone
                keeps it, and a reap frees it); then four fault mutants, each
                of which must fail. The RUNTIME suite above runs the same
                workers over fill grids built, rebuilt and emptied under them,
                adding the installed Statue, Frigate and Dam backgrounds when
                SL_WORLDASSET_ROOT names a tree that has them.

      TOOLS     tools/worldasset/selftest.py. The SLWA writer's own limits,
                that its output parses, that it is deterministic and that
                rerunning replaces rather than appends; the half-float
                reader; and the mesh splitter on a blob laid out by hand.

    NO SOURCE AND NO ROM is opened by any suite - the fixtures are bytes the
    tests write themselves under %TEMP% - so this runs on CI with nothing
    installed, which is the point; the one optional read of installed
    backgrounds is named above and skipped when they are absent.

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
    # v0.4: the fill-query worker pool is linked in for the grid-lifetime part
    # (st_pool_grid), which also reads the installed backgrounds when
    # SL_WORLDASSET_ROOT names a tree that has them.
    & $CC @('-m32', '-O0', '-g', '-Wall', '-Wextra', '-DSL_WA_SELFTEST',
            '-o', $exe, 'src/gfx/sl_gfx_worldasset.c',
            'src/platform/sl_presentation.c', 'src/platform/sl_settings.c',
            'src/platform/sl_worker_pool.c')
    if ($LASTEXITCODE -ne 0) { throw "worldassettest.ps1: compile failed (exit $LASTEXITCODE)" }
    # The provider's own diagnostics (INVALID lines, the summary) go to
    # stderr; cmd owns the redirect (PowerShell 5.1 would turn them into
    # error records). The verdict line is on stdout.
    # THE STARTUP PROVENANCE LABEL IS CHECKED FROM THE PROCESS'S OWN STDERR,
    # because that printed line is the only place the defect was ever
    # observable. summary_once used to pass wa_root() and g_root_from_env as
    # two arguments of ONE fprintf, and C does not order two arguments of a
    # call relative to each other - so the flag could be read before the call
    # that sets it, and being static it then read 0, which is the "player
    # data" label. MEASURED in a far-pair run: the root printed on that line
    # was the one SL_WORLDASSET_ROOT named, while the label beside it said
    # player data.
    #
    # THE FIXTURE ALREADY ARRANGES EXACTLY THE CASE THAT EXPOSES IT, which is
    # why no new harness is needed: it sets SL_WORLDASSET_ROOT to <tmp>\wa,
    # resets g_root_done and g_root_from_env, and only then makes a lookup
    # that reaches summary_once. The lookups before that point run against an
    # INACTIVE provider, and sl_wa_lookup returns at its !sl_wa_active() guard
    # before summary_once, so the line below is genuinely the first one
    # printed and it is printed with the environment root in force.
    #
    # Stderr is captured rather than discarded for this one run. The verdict
    # is still read from the exit code, as before.
    $waErr = Join-Path $OUT 'worldassettest-stderr.txt'
    & cmd /c "`"$exe`" 2>`"$waErr`""
    if ($LASTEXITCODE -ne 0) { $rc = $LASTEXITCODE }

    $prov = @()
    if (Test-Path -LiteralPath $waErr) {
        $prov = @(Get-Content -LiteralPath $waErr -ErrorAction SilentlyContinue |
                  Where-Object { $_ -match '^sl_worldasset: set=' })
    }
    if ($prov.Count -eq 0) {
        Write-Host 'FAIL: no sl_worldasset provenance line was printed, so the label is unchecked'
        $rc = 1
    } else {
        # The root the fixture exported ends in \wa. A line naming that root
        # MUST carry the environment label; a line naming it and saying
        # "player data" is the defect itself.
        $envRooted = @($prov | Where-Object { $_ -match 'root=.*\\wa ' })
        if ($envRooted.Count -eq 0) {
            Write-Host "FAIL: no provenance line named the fixture's own SL_WORLDASSET_ROOT (<tmp>\wa):"
            foreach ($l in $prov) { Write-Host "  $l" }
            $rc = 1
        } else {
            $mislabelled = @($envRooted | Where-Object { $_ -notmatch '\(SL_WORLDASSET_ROOT\)\s*$' })
            if ($mislabelled.Count -gt 0) {
                Write-Host 'FAIL: the root came from SL_WORLDASSET_ROOT and the label disagrees:'
                foreach ($l in $mislabelled) { Write-Host "  $l" }
                $rc = 1
            } else {
                Write-Host ("ok: the startup provenance label agrees with the root it names " +
                            "($($envRooted.Count) line(s))")
            }
        }
    }
    # #70: the skeleton relation and the deformation, linked with the
    # provider (whose parser the skinned item is read by) and the policy.
    $skin = Join-Path $OUT 'charskintest.exe'
    & $CC @('-m32', '-O0', '-g', '-Wall', '-Wextra', '-DSL_CHR_SELFTEST',
            '-o', $skin, 'src/gfx/sl_gfx_charskin.c', 'src/gfx/sl_gfx_worldasset.c',
            'src/platform/sl_presentation.c', 'src/platform/sl_settings.c')
    if ($LASTEXITCODE -ne 0) { throw "worldassettest.ps1: charskin compile failed (exit $LASTEXITCODE)" }
    & cmd /c "`"$skin`" 2>nul"
    if ($LASTEXITCODE -ne 0) { $rc = $LASTEXITCODE }
    # v0.4: the fill-query worker pool on its own - topology and the worker
    # count, random batches, a partial start, a delayed wake across a drain and
    # the shutdown witness - then each fault mutant, which must FAIL the test
    # aimed at it (a pass means the test can no longer see that fault).
    $pool = Join-Path $OUT 'pooltest.exe'
    & $CC @('-m32', '-O0', '-g', '-Wall', '-Wextra', '-DSL_POOL_SELFTEST',
            '-o', $pool, 'src/platform/sl_worker_pool.c')
    if ($LASTEXITCODE -ne 0) { throw "worldassettest.ps1: pool compile failed (exit $LASTEXITCODE)" }
    & cmd /c "`"$pool`" 2>nul"
    if ($LASTEXITCODE -ne 0) { $rc = $LASTEXITCODE }
    # the no-worker path a non-Windows build compiles (SL_POOL_FORCE_STUB)
    $stub = Join-Path $OUT 'pooltest_stub.exe'
    & $CC @('-m32', '-O0', '-g', '-Wall', '-Wextra', '-DSL_POOL_SELFTEST', '-DSL_POOL_FORCE_STUB',
            '-o', $stub, 'src/platform/sl_worker_pool.c')
    if ($LASTEXITCODE -ne 0) { throw "worldassettest.ps1: pool stub compile failed (exit $LASTEXITCODE)" }
    & cmd /c "`"$stub`" 2>nul"
    if ($LASTEXITCODE -ne 0) { $rc = $LASTEXITCODE }
    $mutants = [ordered]@{ OLDSTOP = 'witness'; NOPOOLCHECK = 'witness'; NOCLOSE = 'late'; NOENV = 'random' }
    foreach ($m in $mutants.Keys) {
        $mexe = Join-Path $OUT ("pooltest_" + $m.ToLower() + '.exe')
        & $CC @('-m32', '-O0', '-DSL_POOL_SELFTEST', "-DSL_POOL_MUT_$m", '-o', $mexe, 'src/platform/sl_worker_pool.c')
        if ($LASTEXITCODE -ne 0) { throw "worldassettest.ps1: pool mutant $m compile failed (exit $LASTEXITCODE)" }
        & cmd /c "`"$mexe`" $($mutants[$m]) 1500 >nul 2>nul"
        if ($LASTEXITCODE -eq 0) { Write-Host "FAIL: pool mutant $m passed its test ($($mutants[$m])) - the test no longer sees it"; $rc = 1 }
        else { Write-Host "  pool mutant $m caught by its test ($($mutants[$m]))" }
    }
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
