<#
.SYNOPSIS
    Headless Facility acceptance for the Windows build.

.DESCRIPTION
    Run from an ordinary Windows PowerShell prompt at the repository root:

        .\tools\windows\test.ps1
        .\tools\windows\test.ps1 -Runs 2

    Boots the level with no window, pumps a fixed number of frames and passes
    only when the binary says so itself:

        sightline native: survived <N> pumped frames

    and exits 0. That line is the established evidence that the Windows build
    executes - it is written by the frame pump after it has actually run, so
    it cannot be produced by a process that hung, faulted, or never loaded the
    level. Anything else is a failure, including a zero exit with no line.

    Exits nonzero on any failed run.

.PARAMETER Level
    Level name, resolved through tools/native/levelstage.py. Default facility.

.PARAMETER Frames
    Pumped frames to survive. Default 300, the established figure.

.PARAMETER Runs
    How many times to run it. Default 1.

.PARAMETER TimeoutSeconds
    Kill and fail a run that has not exited by then. Default 180. A hang is a
    failure with evidence, not a test that never returns.
#>
[CmdletBinding()]
param(
    [string]$Level = 'facility',
    [int]$Frames = 300,
    [int]$Runs = 1,
    [int]$TimeoutSeconds = 180
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

$rom = $env:SL_ROM
if (-not $rom) { $rom = Join-Path $repo 'baserom.u.z64' }
if (-not (Test-Path $rom)) {
    Write-Host "sightline: no ROM at '$rom'."
    Write-Host '  Point SL_ROM at your own GoldenEye (U) copy. Nothing ROM-derived'
    Write-Host '  ships in this repository, so there is no fallback to fall back to.'
    exit 1
}
$rom = (Resolve-Path $rom).Path

$python = $env:SL_PYTHON
if (-not $python) { $python = Join-Path $repo '.venv\Scripts\python.exe' }
if (-not (Test-Path $python)) {
    $sys = Get-Command python.exe -ErrorAction SilentlyContinue
    if ($null -eq $sys) {
        Write-Host 'sightline: no Python to resolve the level name with. Run .\tools\windows\setup.ps1.'
        exit 1
    }
    $python = $sys.Source
}

$stage = & $python (Join-Path $repo 'tools\native\levelstage.py') $Level
if ($LASTEXITCODE -ne 0 -or -not $stage) {
    Write-Host "sightline: unknown level '$Level'."
    exit 1
}
$stage = "$stage".Trim()

$exe = Join-Path $repo 'build\win32\sightline.exe'
if (-not (Test-Path $exe)) {
    & (Join-Path $PSScriptRoot 'build.ps1')
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $exe)) {
        Write-Host 'sightline: build failed; not testing.'
        exit 1
    }
}

$logDir = Join-Path $repo 'build\win32\tmp'
if (-not (Test-Path $logDir)) { New-Item -ItemType Directory -Force -Path $logDir | Out-Null }

# SL_RUN=0 disables the per-launch run capture: this is a gate, not a session
# worth keeping, and it should not churn the run directory.
$vars = @{
    SL_ROM             = $rom
    SL_BOOT_LEVEL      = $stage
    SL_BOOT_DIFFICULTY = '1'
    SL_WINDOW          = '0'
    SL_FRAMES          = "$Frames"
    SL_RUN             = '0'
}
$expect = "survived $Frames pumped frames"

Write-Host ""
Write-Host "Sightline headless acceptance - $Level (stage $stage), $Frames frames, $Runs run(s)"
Write-Host "  expecting: `"$expect`" and exit 0"
Write-Host ""

$failures = 0
for ($i = 1; $i -le $Runs; $i++) {
    # System.Diagnostics.Process rather than Start-Process, for two MEASURED
    # reasons.
    #
    #   EXIT CODE. Start-Process -PassThru WITHOUT -Wait never populates
    #   .ExitCode - PowerShell does not retain the process handle - so it read
    #   back EMPTY and every run failed with "exit " while the expected line
    #   was present. Start-Process -Wait does populate it but offers no
    #   timeout, and a gate that can hang forever is not a gate. Creating the
    #   Process here keeps the handle and gives both.
    #
    #   ENVIRONMENT. psi.EnvironmentVariables configures the CHILD, so the
    #   calling shell's SL_* variables are never touched, let alone left
    #   behind if this script is interrupted.
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName               = $exe
    $psi.UseShellExecute        = $false
    $psi.WorkingDirectory       = $repo
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError  = $true
    foreach ($k in $vars.Keys) { $psi.EnvironmentVariables[$k] = $vars[$k] }

    $p = New-Object System.Diagnostics.Process
    $p.StartInfo = $psi
    [void]$p.Start()
    # Drain both pipes concurrently. Reading one to the end while the other
    # fills its buffer is the classic deadlock, and this build is verbose.
    $outTask = $p.StandardOutput.ReadToEndAsync()
    $errTask = $p.StandardError.ReadToEndAsync()

    if (-not $p.WaitForExit($TimeoutSeconds * 1000)) {
        try { $p.Kill() } catch { }
        $p.WaitForExit()
        Write-Host ("  run {0}: FAIL - no exit within {1}s (killed)" -f $i, $TimeoutSeconds)
        $failures++
        continue
    }
    $code = $p.ExitCode
    $text = $errTask.Result + $outTask.Result
    $p.Dispose()

    # Keep the evidence on disk either way; a passing run is still the thing
    # you compare the next failing one against.
    $errFile = Join-Path $logDir "test.$i.log"
    [System.IO.File]::WriteAllText($errFile, $text, (New-Object System.Text.UTF8Encoding($false)))

    $sawLine = $text.Contains($expect)
    if ($sawLine -and $code -eq 0) {
        Write-Host ("  run {0}: PASS - `"{1}`", exit 0" -f $i, $expect)
    } else {
        $failures++
        Write-Host ("  run {0}: FAIL - exit {1}, expected line {2}" -f `
                    $i, $code, $(if ($sawLine) { 'present' } else { 'ABSENT' }))
        $tail = @($text -split "`n" | Where-Object { $_.Trim() -ne '' } | Select-Object -Last 20)
        foreach ($l in $tail) { Write-Host "      | $($l.TrimEnd())" }
        Write-Host "      full output: $errFile"
    }
}

Write-Host ""
if ($failures -eq 0) {
    Write-Host "PASS: $Runs/$Runs"
    Write-Host ""
    exit 0
}
Write-Host "FAIL: $failures of $Runs run(s)"
Write-Host ""
exit 1
