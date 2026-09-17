<#
.SYNOPSIS
    Raw parity runner. Reproduces the EXACT launch an agent uses when it
    measures boot and frontend behaviour.

.DESCRIPTION
    This is not play.ps1 and must never grow into it.

    It exists for one reason: the owner and every agent must run the SAME
    FILE, so that "what I see" and "what you see" are the same execution.
    Agents are FORBIDDEN from launching build\win32\sightline.exe through an
    ad-hoc Start-Process for boot or frontend measurements. Run this instead.

    Deliberately absent, and to stay absent:
      * no build, ever (see NO AUTO-REBUILD below)
      * no level resolution, difficulty, control or mouse preferences
      * no player instructions, no convenience defaults
      * no screenshot, silhouette, timing, census or debug instrumentation
      * no logging system beyond the two redirect files
      * no SL_RUN override - the validated run did not set it, so this
        file does not either; the child inherits the ambient state
      * no blanket clearing of the SL_* environment

    NO AUTO-REBUILD is the whole point. The purpose is to run exactly the
    bytes the agent measured. A missing exe is an error and a stale exe is a
    warning - never a rebuild, because rebuilding would run different bytes
    than the ones under discussion.

    NO -NoNewWindow is also deliberate. The clean agent runs this file
    reproduces did not pass it. An extra console window IS the parity
    configuration; do not "fix" it.

    Ambient SL_* variables are ENUMERATED and PRINTED, not swept. Sweeping
    them would build a cleaner experiment rather than reproduce the original
    one. Only the variables named below are overridden, and only SL_SHOT is
    removed. The prior environment is restored afterwards.

    Usage:  .\tools\windows\agent-parity.ps1
    No parameters. Adding one that could alter the launch defeats the file.
#>

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$exe  = Join-Path $repo 'build\win32\sightline.exe'
$bin  = Split-Path -Parent $exe

# ---------------------------------------------------------------- executable
if (-not (Test-Path $exe)) {
    Write-Host "agent-parity: no executable at '$exe'."
    Write-Host '  Build first:  .\tools\windows\build.ps1'
    Write-Host '  This runner never builds. It exists to run exactly the bytes'
    Write-Host '  the agent measured, and a build would replace them.'
    exit 1
}
$exeItem = Get-Item $exe

function Get-Sha256Lower([string]$path) {
    if (-not (Test-Path $path)) { return $null }
    return (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash.ToLower()
}

$exeSha  = Get-Sha256Lower $exe
$sdlSha  = Get-Sha256Lower (Join-Path $bin 'SDL2.dll')
$pthSha  = Get-Sha256Lower (Join-Path $bin 'libwinpthread-1.dll')
if (-not $sdlSha) { $sdlSha = 'not present' }
if (-not $pthSha) { $pthSha = 'not present' }

# Staleness is REPORTED, never acted on.
$stale  = $null
$srcDir = Join-Path $repo 'src'
if (Test-Path $srcDir) {
    $newest = Get-ChildItem -LiteralPath $srcDir -Recurse -File -ErrorAction SilentlyContinue |
              Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($newest -and $newest.LastWriteTime -gt $exeItem.LastWriteTime) { $stale = $newest }
}

# ----------------------------------------------------------------------- ROM
$rom = $env:SL_ROM
if (-not $rom) { $rom = Join-Path $repo 'baserom.u.z64' }
if (-not (Test-Path $rom)) {
    Write-Host "agent-parity: no ROM at '$rom'."
    Write-Host '  Point SL_ROM at your own GoldenEye (U) copy. Nothing ROM-derived'
    Write-Host '  ships in this repository, so there is no fallback.'
    exit 1
}
$rom = (Resolve-Path $rom).Path

# ------------------------------------------------- scratch dir, save, outputs
# Never the owner's real save at <LOCALAPPDATA>\sightline\eeprom.bin. This is a
# throwaway cartridge in a sibling directory, outside git.
$base = $env:LOCALAPPDATA
if (-not $base -and $env:USERPROFILE) { $base = Join-Path $env:USERPROFILE 'AppData\Local' }
if (-not $base) { $base = $env:TEMP }
if (-not $base) {
    Write-Host 'agent-parity: none of LOCALAPPDATA, USERPROFILE or TEMP is set.'
    Write-Host '  There is nowhere outside the repository to put the scratch EEPROM.'
    exit 1
}
$scratch = Join-Path $base 'sightline\agent-parity'
New-Item -ItemType Directory -Force -Path $scratch | Out-Null

$stamp   = Get-Date -Format 'yyyyMMdd-HHmmss'
$save    = Join-Path $scratch 'eeprom.bin'
$outPath = Join-Path $scratch "$stamp.out.txt"
$errPath = Join-Path $scratch "$stamp.err.txt"
$manPath = Join-Path $scratch "$stamp.manifest.txt"

# --------------------------------------------------------------- environment
# Overridden by name. Everything else is INHERITED untouched, and the ambient
# SL_* set is printed so an unexpected one is visible rather than silent.
$vars = [ordered]@{
    SL_ROM         = $rom
    SL_WINDOW      = '1'
    SL_WINDOW_SIZE = '960x720'
    SL_EEPROM_RW   = $save
    SL_FRAMES      = '1600'
}
$removeVars = @('SL_SHOT')

$ambient   = Get-ChildItem Env: | Where-Object { $_.Name -like 'SL_*' } | Sort-Object Name
$inherited = $ambient | Where-Object { -not $vars.Contains($_.Name) -and $removeVars -notcontains $_.Name }

if ($null -eq $env:SL_SHOT) { $shotState = 'not set (nothing to remove)' }
else { $shotState = "REMOVED for the child (ambient value was '$env:SL_SHOT')" }

# SL_RUN is NOT set by this script. The validated run did not set it, so the
# child inherits whatever the environment has. Reported, never forced.
if ($null -eq $env:SL_RUN) { $runState = 'not set by this script, not present ambiently' }
else { $runState = "not set by this script; ambient value '$env:SL_RUN' is INHERITED" }

# ------------------------------------------------------------------ manifest
$m = New-Object System.Collections.Generic.List[string]
$m.Add('=== sightline agent parity run ===================================')
$m.Add("  when                 $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')")
$m.Add("  script               $PSCommandPath")
$m.Add("  repo                 $repo")
$m.Add("  cwd (child)          $repo")
$m.Add('  --- binary identity ---')
$m.Add("  exe                  $exe")
$m.Add("  exe size             $($exeItem.Length) bytes")
$m.Add("  exe last write       $($exeItem.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'))")
$m.Add("  exe sha256           $exeSha")
$m.Add("  SDL2.dll sha256      $sdlSha")
$m.Add("  libwinpthread sha256 $pthSha")
if ($stale) {
    $m.Add("  STALE                yes - $($stale.FullName) is newer than the exe")
    $m.Add('                       NOT rebuilding. Running the measured bytes.')
} else {
    $m.Add('  stale                no - nothing under src/ is newer than the exe')
}
$m.Add('  --- environment (overridden) ---')
foreach ($k in $vars.Keys) { $m.Add(("  {0,-20} {1}" -f $k, $vars[$k])) }
$m.Add("  SL_SHOT              $shotState")
$m.Add("  SL_RUN               $runState")
$m.Add('  --- environment (inherited, NOT overridden) ---')
if ($inherited) { foreach ($e in $inherited) { $m.Add(("  {0,-20} {1}" -f $e.Name, $e.Value)) } }
else { $m.Add('  (none)') }
$m.Add('  --- redirect files ---')
$m.Add("  stdout               $outPath")
$m.Add("  stderr               $errPath")
$m.Add("  manifest             $manPath")
$m.Add('  --- process creation ---')
$m.Add('  method               Start-Process')
$m.Add('  redirects            yes (stdout and stderr)')
$m.Add('  -Wait                yes')
$m.Add('  -PassThru            yes')
$m.Add('  -NoNewWindow         NO  (deliberate: the clean agent run did not use it)')
$m.Add('==================================================================')

$text = ($m -join [Environment]::NewLine)
Write-Host $text
Set-Content -LiteralPath $manPath -Value $text -Encoding utf8

# -------------------------------------------------------------------- launch
$prior = @{}
foreach ($k in $vars.Keys)  { $prior[$k] = [Environment]::GetEnvironmentVariable($k, 'Process') }
foreach ($k in $removeVars) { $prior[$k] = [Environment]::GetEnvironmentVariable($k, 'Process') }

$exitCode = $null
try {
    foreach ($k in $vars.Keys)  { [Environment]::SetEnvironmentVariable($k, $vars[$k], 'Process') }
    foreach ($k in $removeVars) { [Environment]::SetEnvironmentVariable($k, $null, 'Process') }

    $p = Start-Process -FilePath $exe `
                       -WorkingDirectory $repo `
                       -RedirectStandardOutput $outPath `
                       -RedirectStandardError $errPath `
                       -PassThru -Wait
    $exitCode = $p.ExitCode
}
finally {
    foreach ($k in $prior.Keys) { [Environment]::SetEnvironmentVariable($k, $prior[$k], 'Process') }
}

$outLen = 0
$errLen = 0
if (Test-Path $outPath) { $outLen = (Get-Item $outPath).Length }
if (Test-Path $errPath) { $errLen = (Get-Item $errPath).Length }

Write-Host ''
Write-Host "  exit code            $exitCode"
Write-Host "  stdout bytes         $outLen  ($outPath)"
Write-Host "  stderr bytes         $errLen  ($errPath)"
Write-Host "  manifest             $manPath"

exit $exitCode
