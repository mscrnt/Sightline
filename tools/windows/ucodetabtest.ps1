<#
.SYNOPSIS
    Prove the run-time audio microcode tables byte-identical to an independent
    derivation, and prove a changed ROM is refused.

.DESCRIPTION
    Run from an ordinary Windows PowerShell prompt at the repository root:

        .\tools\windows\ucodetabtest.ps1

    v0.2.0 moved the ENVMIXER ramp and the RESAMPLE polyphase table out of
    the executable (they used to be compiled in from a locally extracted
    microcode segment) and into a start-up derivation from the player's own
    ROM (src/platform/sl_ucode.c). This is the focused test of that seam:

      1. IDENTITY. tools/native/ucode_tables_oracle.py derives the two tables
         with the standard library's zlib; the executable is run once with
         SL_UCODE_TABLES_DUMP and its in-memory tables are compared byte for
         byte (Compare-Object over the bytes, and SHA-256) with the oracle's.
      2. NEGATIVE CONTROL. A scratch copy of the ROM with ONE byte changed
         inside the compressed segment must be refused: exit 3, the
         "cannot derive" message, no dump written.
      3. NEGATIVE CONTROL. A truncated file must be refused the same way.

    Every ROM copy and every dump lands in a fresh directory under %TEMP%
    and is deleted at the end; the player's config and saves are never
    opened (SL_CONFIG and LOCALAPPDATA point into the scratch directory).
    Exits 0 when every check passes, 1 otherwise.

.PARAMETER Rom
    The ROM. Default: SL_ROM, else baserom.u.z64 in the repository root.

.PARAMETER Exe
    The executable. Default: build\win32\sightline.exe (built if missing).
#>
[CmdletBinding()]
param(
    [string]$Rom,
    [string]$Exe
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

if (-not $Rom) { $Rom = $env:SL_ROM }
if (-not $Rom) { $Rom = Join-Path $repo 'baserom.u.z64' }
if (-not (Test-Path -LiteralPath $Rom)) { Write-Host "ucodetabtest: no ROM at '$Rom'."; exit 1 }
$Rom = (Resolve-Path -LiteralPath $Rom).Path

if (-not $Exe) { $Exe = Join-Path $repo 'build\win32\sightline.exe' }
if (-not (Test-Path -LiteralPath $Exe)) {
    & (Join-Path $PSScriptRoot 'build.ps1')
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $Exe)) { Write-Host 'ucodetabtest: build failed.'; exit 1 }
}
$Exe = (Resolve-Path -LiteralPath $Exe).Path

$python = $env:SL_PYTHON
if (-not $python) { $python = Join-Path $repo '.venv\Scripts\python.exe' }
if (-not (Test-Path $python)) {
    $sys = Get-Command python.exe -ErrorAction SilentlyContinue
    if ($null -eq $sys) { Write-Host 'ucodetabtest: no Python. Run .\tools\windows\setup.ps1.'; exit 1 }
    $python = $sys.Source
}

$stage = (& $python (Join-Path $repo 'tools\native\levelstage.py') 'facility').Trim()
if ($LASTEXITCODE -ne 0 -or -not $stage) { Write-Host 'ucodetabtest: levelstage.py could not resolve facility.'; exit 1 }

$scratch = Join-Path $env:TEMP ('sl-ucodetab-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $scratch | Out-Null
$pass = 0; $fail = 0
function Check { param([bool]$Ok, [string]$What)
    if ($Ok) { $script:pass++; Write-Host "  ok    $What" } else { $script:fail++; Write-Host "  FAIL  $What" }
}

function Invoke-Sightline {
    param([string]$RomPath, [string]$Dump)
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $Exe
    $psi.UseShellExecute = $false
    $psi.WorkingDirectory = $scratch
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $vars = @{
        SL_ROM = $RomPath; SL_WINDOW = '0'; SL_FRAMES = '5'; SL_RUN = '0'; SL_AUDIO = '0'
        SL_BOOT_LEVEL = $stage; SL_BOOT_DIFFICULTY = '1'
        SL_CONFIG = (Join-Path $scratch 'cfg\config.ini')
        SL_EEPROM_RW = (Join-Path $scratch 'lad\eeprom.bin')
        LOCALAPPDATA = (Join-Path $scratch 'lad')
        SL_UCODE_TABLES_DUMP = $Dump
    }
    foreach ($k in $vars.Keys) { $psi.EnvironmentVariables[$k] = $vars[$k] }
    $p = New-Object System.Diagnostics.Process
    $p.StartInfo = $psi
    [void]$p.Start()
    $o = $p.StandardOutput.ReadToEndAsync(); $e = $p.StandardError.ReadToEndAsync()
    if (-not $p.WaitForExit(120000)) { try { $p.Kill() } catch { }; $p.WaitForExit(); return @{ code = -1; text = 'TIMEOUT' } }
    $r = @{ code = $p.ExitCode; text = ($e.Result + $o.Result) }
    $p.Dispose()
    return $r
}

try {
    Write-Host ''
    Write-Host "ucodetabtest: oracle derivation (standard-library zlib)"
    $oracleDir = Join-Path $scratch 'oracle'
    & $python (Join-Path $repo 'tools\native\ucode_tables_oracle.py') $Rom $oracleDir | ForEach-Object { Write-Host "  $_" }
    if ($LASTEXITCODE -ne 0) { Write-Host 'ucodetabtest: the oracle refused the ROM.'; exit 1 }

    Write-Host "ucodetabtest: 1. identity - the executable's run-time tables against the oracle"
    $dump = Join-Path $scratch 'rt'
    $r = Invoke-Sightline -RomPath $Rom -Dump $dump
    [System.IO.File]::WriteAllText((Join-Path $scratch 'run-identity.log'), $r.text)
    Check ($r.code -eq 0) ("executable exit 0 (got {0})" -f $r.code)
    Check ($r.text.Contains('sightline ucode: audio microcode tables derived from the ROM at start-up')) 'the derivation line was printed'
    Check ((Test-Path "$dump.ramp") -and (Test-Path "$dump.taps")) 'the dump files were written'
    if ((Test-Path "$dump.ramp") -and (Test-Path "$dump.taps")) {
        $rtRamp = [System.IO.File]::ReadAllBytes("$dump.ramp"); $orRamp = [System.IO.File]::ReadAllBytes((Join-Path $oracleDir 'ramp.bin'))
        $rtTaps = [System.IO.File]::ReadAllBytes("$dump.taps"); $orTaps = [System.IO.File]::ReadAllBytes((Join-Path $oracleDir 'taps.bin'))
        Check ($rtRamp.Length -eq 16 -and $orRamp.Length -eq 16) ("ENVMIXER ramp is 16 bytes on both sides ({0} / {1})" -f $rtRamp.Length, $orRamp.Length)
        Check ($rtTaps.Length -eq 512 -and $orTaps.Length -eq 512) ("RESAMPLE table is 512 bytes on both sides ({0} / {1})" -f $rtTaps.Length, $orTaps.Length)
        $dRamp = @(Compare-Object -ReferenceObject $orRamp -DifferenceObject $rtRamp -SyncWindow 0)
        $dTaps = @(Compare-Object -ReferenceObject $orTaps -DifferenceObject $rtTaps -SyncWindow 0)
        Check ($dRamp.Count -eq 0) ("ENVMIXER ramp byte-identical (Compare-Object differences: {0})" -f $dRamp.Count)
        Check ($dTaps.Count -eq 0) ("RESAMPLE table byte-identical (Compare-Object differences: {0})" -f $dTaps.Count)
        $hRamp = (Get-FileHash "$dump.ramp" -Algorithm SHA256).Hash.ToLower(); $hRampO = (Get-FileHash (Join-Path $oracleDir 'ramp.bin') -Algorithm SHA256).Hash.ToLower()
        $hTaps = (Get-FileHash "$dump.taps" -Algorithm SHA256).Hash.ToLower(); $hTapsO = (Get-FileHash (Join-Path $oracleDir 'taps.bin') -Algorithm SHA256).Hash.ToLower()
        Check ($hRamp -eq $hRampO) "ENVMIXER ramp SHA-256 $hRamp == oracle"
        Check ($hTaps -eq $hTapsO) "RESAMPLE table SHA-256 $hTaps == oracle"
    }

    Write-Host "ucodetabtest: 2. negative control - one byte changed inside the compressed segment"
    $bad = Join-Path $scratch 'changed.z64'
    $bytes = [System.IO.File]::ReadAllBytes($Rom)
    $at = 137616 + 4096
    $bytes[$at] = [byte]($bytes[$at] -bxor 0x01)
    [System.IO.File]::WriteAllBytes($bad, $bytes)
    $dump2 = Join-Path $scratch 'rt2'
    $r2 = Invoke-Sightline -RomPath $bad -Dump $dump2
    [System.IO.File]::WriteAllText((Join-Path $scratch 'run-changed.log'), $r2.text)
    Check ($r2.code -eq 3) ("refused with exit 3 (got {0})" -f $r2.code)
    Check ($r2.text.Contains('sightline ucode: cannot derive the audio microcode tables from the ROM')) 'the refusal names the cause'
    Check ($r2.text.Contains('not the supported dump')) 'the refusal is the compressed-segment digest check'
    Check (-not (Test-Path "$dump2.ramp") -and -not (Test-Path "$dump2.taps")) 'no tables were dumped'
    Check (-not $r2.text.Contains('booting via mainproc')) 'the game did not boot'

    Write-Host "ucodetabtest: 3. negative control - a truncated file"
    $trunc = Join-Path $scratch 'short.z64'
    [System.IO.File]::WriteAllBytes($trunc, $bytes[0..1048575])
    $dump3 = Join-Path $scratch 'rt3'
    $r3 = Invoke-Sightline -RomPath $trunc -Dump $dump3
    Check ($r3.code -eq 3) ("refused with exit 3 (got {0})" -f $r3.code)
    Check ($r3.text.Contains('a plain .z64 dump is 12582912')) 'the refusal names the size'
    Check (-not (Test-Path "$dump3.ramp")) 'no tables were dumped'
}
finally {
    Remove-Item -Recurse -Force -LiteralPath $scratch -ErrorAction SilentlyContinue
}

Write-Host ''
Write-Host ("ucodetabtest: {0} passed, {1} failed" -f $pass, $fail)
if ($fail -ne 0) { exit 1 }
exit 0
