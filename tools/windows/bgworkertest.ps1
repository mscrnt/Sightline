<#
.SYNOPSIS
    Build and run the background vertex helper's self-test (v0.4, B-PERF1).

.DESCRIPTION
    Run from an ordinary Windows PowerShell prompt at the repository root:

        .\tools\windows\bgworkertest.ps1

    src/platform/sl_bg_worker.c compiled with -DSL_BGW_SELFTEST: the shipped
    functions, with fault injection for the native waits and signals, a thread
    creation failure and a floating-point environment that does not take.

      main          3000 jobs with disjoint halves, both complete after the join;
                    a second post in flight refused; a join with nothing posted
                    claims nothing; the posting thread's x87 control word and
                    MXCSR in the helper; an FP environment that does not take is
                    proven complete with nothing run
      startfail     a failed thread creation leaves no helper and is not retried
      waitfail      a slow writer and a failed wait: the join claims nothing, the
                    helper is lost (no post, no restart) and the writer completes
                    into memory that was never released
      setgofail     a wake that cannot be signalled is lost, not posted
      setdonefail   a lost completion signal is still proven by the generation
      helperexit    a helper that ends without completing is lost
      stopdeadline  one deadline covers drain and exit: a slow writer outlives a
                    short stop (retained, no post, no restart), a repeated stop
                    proves completion and exit, a further stop is a no-op
      stopdrain     a stop right after a post drains the job first

    Then the no-helper build (SL_BGW_FORCE_STUB, what a non-Windows build gets)
    and four fault mutants, each of which must FAIL the mode aimed at it:
    FAILOPEN (a failed wait reported as success) waitfail, NOFP (no FP load)
    main, NOWAIT (the join does not wait) main, STOPFREE (a stop that releases
    without proof) stopdeadline.

    Exits 0 when every check passes, 1 otherwise. Same toolchain ladder as
    build.ps1 / worldassettest.ps1.

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
    Write-Error "bgworkertest.ps1: no mingw32 toolchain found. Run tools\windows\setup.ps1."
}
$CC = Join-Path $mingwBin 'i686-w64-mingw32-gcc.exe'

New-Item -ItemType Directory -Force -Path $OUT | Out-Null
# the shipped build's floating-point flags (build.ps1's native set)
$flags = @('-m32', '-O2', '-msse2', '-mfpmath=sse', '-DSL_BGW_SELFTEST')

$rc = 0
$savedPath = $env:PATH
$savedCwd  = (Get-Location).Path
try {
    $env:PATH = "$mingwBin;$env:PATH"
    Set-Location $repo
    $exe = Join-Path $OUT 'bgworkertest.exe'
    & $CC @($flags + @('-Wall', '-Wextra', '-o', $exe, 'src/platform/sl_bg_worker.c'))
    if ($LASTEXITCODE -ne 0) { throw "bgworkertest.ps1: compile failed (exit $LASTEXITCODE)" }
    foreach ($mode in 'main', 'startfail', 'waitfail', 'setgofail', 'setdonefail', 'helperexit', 'stopdeadline', 'stopdrain') {
        & $exe $mode
        if ($LASTEXITCODE -ne 0) { $rc = 1 }
    }
    $stub = Join-Path $OUT 'bgworkertest_stub.exe'
    & $CC @($flags + @('-Wall', '-Wextra', '-DSL_BGW_FORCE_STUB', '-o', $stub, 'src/platform/sl_bg_worker.c'))
    if ($LASTEXITCODE -ne 0) { throw "bgworkertest.ps1: stub compile failed (exit $LASTEXITCODE)" }
    & $stub
    if ($LASTEXITCODE -ne 0) { $rc = 1 }
    $mutants = [ordered]@{ FAILOPEN = 'waitfail'; NOFP = 'main'; NOWAIT = 'main'; STOPFREE = 'stopdeadline' }
    foreach ($m in $mutants.Keys) {
        $mexe = Join-Path $OUT ('bgworkertest_' + $m.ToLower() + '.exe')
        & $CC @($flags + @("-DSL_BGW_MUT_$m", '-o', $mexe, 'src/platform/sl_bg_worker.c'))
        if ($LASTEXITCODE -ne 0) { throw "bgworkertest.ps1: mutant $m compile failed (exit $LASTEXITCODE)" }
        & cmd /c "`"$mexe`" $($mutants[$m]) >nul 2>nul"
        if ($LASTEXITCODE -eq 0) { Write-Host "FAIL: mutant $m passed its test ($($mutants[$m])) - the test no longer sees it"; $rc = 1 }
        else { Write-Host "  mutant $m caught by its test ($($mutants[$m]))" }
    }
} finally {
    Set-Location $savedCwd
    $env:PATH = $savedPath
}
exit $rc
