<#
.SYNOPSIS
    Run the OPTIMIZED RENDERING row's CPU checks in one batch and write one
    machine-readable result table.

.DESCRIPTION
    Run from an ordinary Windows PowerShell prompt at the repository root:

        .\tools\windows\perfchecks.ps1
        .\tools\windows\perfchecks.ps1 -Only perf,skybatch
        .\tools\windows\perfchecks.ps1 -Out C:\somewhere\new-dir

    It is a RUNNER, not a test: every check it runs is one of the tree's own
    existing test entrypoints, invoked unchanged. What this adds is one place
    that runs them in sequence and leaves evidence that can be audited without
    re-running anything:

      * each check's exact command, working directory, start and end time, and
        its REAL integer OS exit code, read from the process object - never
        inferred from this script's own exit;
      * each check's full raw stdout and stderr, in its own file;
      * a short excerpt of the failing lines for any check that failed;
      * the SHA256 of every source file the checks exercise, and of the build
        outputs if they are present, so a result can be tied to the code it
        was taken from;
      * one result.json and one result.md holding the whole table.

    A check that fails does NOT stop the batch: every row is attempted, and the
    script's own exit is nonzero if any row FAILED. A child's failure can
    therefore never disappear behind this script's exit code.

    THREE STATUSES, kept apart on purpose:

      PASS         the check ran and its process exited 0.
      FAIL         the check ran and its process exited nonzero, or could not
                   be started. The excerpt says what it printed.
      NOT_COVERED  evidence this batch cannot produce at all, listed as a row
                   so it is visible rather than absent. Every NOT_COVERED row
                   names what would establish it. Nothing here is a tolerance,
                   a waiver or a pass by another name.

    No game, no window, no GL context, no GPU and no ROM: every check in the
    table is a host-side self-test or an offline fixture against a synthetic
    GL stub. -Only reruns a subset, for the case where one row failed and was
    corrected.

.PARAMETER Out
    Where to write the evidence. Default build\win32\perfchecks. An existing
    directory is reused and its files for the rows being run are overwritten;
    rows not being run keep theirs, which is what makes -Only a rerun rather
    than a fresh batch.

.PARAMETER Only
    Run just these rows, by name. Default: every runnable row.

.PARAMETER Msys2Root
    Where MSYS2 lives, if it is not in one of the usual places. Passed
    through to each check that takes it.
#>
[CmdletBinding()]
param(
    [string]$Out,
    [string[]]$Only,
    [string]$Msys2Root
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $Out) { $Out = Join-Path $repo 'build\win32\perfchecks' }
New-Item -ItemType Directory -Force -Path $Out | Out-Null

# ---- the table -----------------------------------------------------------
#
# Each runnable row names an entrypoint that already exists in this tree. The
# `covers` text is what the row's own evidence establishes, in the vocabulary
# of the coverage areas, so a reader can tell which area a FAIL belongs to.

$rows = @(
    @{ name = 'settings';      script = 'tools\windows\settingstest.ps1';
       covers = 'the settings store: the new optimized_rendering row persists, reloads, defaults to 1 on a missing / pre-key / malformed / out-of-range file, refuses an out-of-range set, and sits beside every other row without disturbing it' },
    @{ name = 'perf';          script = 'tools\windows\perftest.ps1';
       covers = 'the shared policy: default ON in a player session, OFF for an inactive store (trace replay / headless / matching), write-on-change and read-back, invalid sets refused, the SL_PERF_OPT launch override and the set that ends it, each subsystem switch governing its own subsystem only, the far-walk arm, and the carrier the row selects (prod, never byte or div)' },
    @{ name = 'display';       script = 'tools\windows\displaytest.ps1';
       covers = 'the display rows the new row sits beside, over the same store: the aspect enum, the viewport fit, the window modes and lists, and the editors steps - a regression check on the store the new row was appended to' },
    @{ name = 'presentation';  script = 'tools\windows\presentationtest.ps1';
       covers = 'the other policy over the same store (the PRESENTATION row), unchanged - a regression check that appending a row did not move an existing one' },
    @{ name = 'skybatch';      script = 'tools\windows\skybatchtest.ps1';
       covers = 'the skydome pass against a synthetic GL stub: the row drives the path, OFF -> ON -> OFF -> ON on the SAME asset, the carrier the row selects and its record size / stride / offsets, one coherent carrier latched per pass, float-staging allocation failure falling back to the original path with the GL bracket intact, context recreation, the state bracket, no sync point, and one prepared record per unique referenced vertex' },
    @{ name = 'bggpu';         script = 'tools\windows\bggputest.ps1';
       covers = 'the background GPU transform against a synthetic GL stub: the row drives it and OFF does not poison capability, a real capability failure is latched and never retried, context recreation, the walk uniform upload, and the depth uniform following the mesh and the range across opaque / blended / opaque at FAR0 without re-binding and across FAR1 and SINGLE' },
    @{ name = 'perflive';      script = 'tools\windows\perflivetest.ps1';
       covers = 'the live batch runner and its result checker, against synthetic fake-child output trees: the scratch config and asset roots, the pinned RNG state and paired initial bytes, a witness that a diagnostic does not disable the feature it measures, the unsound-masking chronology in BOTH orders, capture-set exactness (missing / truncated / extra / wrong dimensions / wrong-but-identical), subset versus full qualification, discovery failure, a stale attempt directory, a counters-only selection, and per-arm receipts on exception and timeout. Every negative is also run against a re-implementation of the defective rule, which the suite requires to PASS - so each check is measured to bite rather than asserted to' },
    @{ name = 'gpudepthprobe'; script = 'tools\windows\gpudepthprobe.ps1';
       covers = 'the driver depth probe SETUP, built but NOT executed against a GL driver: the production vertex program text and its uniform names are extracted from src/gfx/sl_gfx_bggpu.c and pinned by hash (a shader-text edit refuses the build), the names are cross-checked against the ones the module actually resolves, the pinned bytes are proven equal to the bytes the module hands glShaderSource, and the CPU reference - the original expressions with the original flags, built with the shipping build''s own -msse2 -mfpmath=sse so the arithmetic matches - is compared bit for bit with no tolerance over 34 synthetic cases covering the SINGLE clamp''s boundary, the FAR0 range ends and the opaque-versus-blend coefficient pair, and FAR1''s independence of both. The comparator is shown to bite: the same loop the driver path uses rejects the wrong coefficient pair, a one-ulp error in either coefficient, and each depth case offered as another. Running it against a real driver is an explicit opt-in flag this row does not pass' },
    @{ name = 'worldasset';    script = 'tools\windows\worldassettest.ps1';
       covers = 'the CPU side of the far ranges: sl_wa_far_plane and sl_wa_far_range, the two overlap constants, and the near / far coefficient arithmetic the GPU path uploads as uniforms' },
    @{ name = 'bgworker';      script = 'tools\windows\bgworkertest.ps1';
       covers = 'the background vertex helper the CPU work runs on: its start / wait / drain / failure paths and their fallbacks, which this row does not reach in either position and which must therefore be unchanged' }
)

# ---- what this batch cannot establish, named rather than omitted ---------

$notcovered = @(
    @{ name = 'gpu-depth-images';
       why  = 'nonzero submitted draw calls AND indices for SL_BGGPU_DEPTH_SINGLE, FAR0 and FAR1 together with frame-matched OFF / ON images';
       needs = 'a live run on a GL driver. The counters that would carry it already exist (g_bggpu_dcalls / g_bggpu_didx, reported by SL_DL_CENSUS); a census arm can establish which depth modes submitted, and cannot establish anything about the CPU loop reduction, because the census itself refuses a reduced walk (fstats). The two therefore need separate arms.' },
    @{ name = 'far-shader-arithmetic';
       why  = 'the far clamp and the FAR0 / FAR1 depth expressions as a driver actually evaluates them, and the depth-buffer ordering that follows';
       needs = 'a live run. The offline bggpu fixture records only that the program text was handed to glShaderSource; its uniform checks establish which values the program is GIVEN and qualify nothing about the GLSL. A source-level transcription agreeing with the CPU reference is an agreement between two texts.' },
    @{ name = 'depth-sequencing';
       why  = 'FAR0 -> FAR1 preserving the save / set-clear-depth / depth-mask / clear / restore sequence in an image, the late pass not clearing, and later game geometry occluding correctly';
       needs = 'a live run with geometry in front of the background, not an empty-sky frame.' },
    @{ name = 'movement-boundaries';
       why  = 'frame identity while turning and walking across the near / far transitions';
       needs = 'a live run through the existing capture and oracle machinery, with the poses preselected by actual depth submissions.' },
    @{ name = 'combined-mode';
       why  = 'the row ON selecting the auto workers, the background GPU transform, the dependent reduced CPU loop and the sky PROD carrier together, with matching images, reduced walks > 0 and zero unsound draws';
       needs = 'a live run, in a clean arm separate from any census arm. No record of a prior combined-mode qualification exists.' },
    @{ name = 'menu-ui';
       why  = 'both menu rows reachable by keyboard, controller and pointer, the label clearing its value column in each, save and reload across a restart, and a stage transition and presentation cycle with no stale world assets';
       needs = 'a live run. The label widths come from font tables read out of the ROM at run time and cannot be measured offline.' },
    @{ name = 'performance';
       why  = 'a measured frame-time difference between the row OFF and the row ON on a matched workload';
       needs = 'a live run. No frame time is claimed anywhere in this change.' }
)

if ($Only) { $rows = @($rows | Where-Object { $Only -contains $_.name }) }
if ($rows.Count -eq 0) { Write-Error 'perfchecks.ps1: -Only matched no runnable row.' }

# ---- pins ----------------------------------------------------------------

function Get-Pin {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return @{ path = $Path; present = $false }
    }
    $item = Get-Item -LiteralPath $Path
    $h = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLower()
    return @{ path = $Path; present = $true; bytes = $item.Length; sha256 = $h }
}

$sourcePins = @()
foreach ($rel in @(
    'src/platform/sl_perf.c', 'src/platform/sl_perf.h',
    'src/platform/sl_settings.c', 'src/platform/sl_settings.h',
    'src/gfx/sl_gfx_skybatch.c', 'src/gfx/sl_gfx_skybatch.h',
    'src/gfx/sl_gfx_bggpu.c', 'src/gfx/sl_gfx_bggpu.h',
    'src/gfx/sl_gfx_dl.c',
    'src/native/sl_front_options.c',
    'src/game/options.c', 'src/game/options.h',
    'tools/profiling/native/sl_gfx_skybatch_testgl.h',
    'tools/profiling/native/sl_gfx_skybatch_testgl.c',
    'tools/profiling/native/test_sl_gfx_skybatch.c',
    'tools/profiling/native/sl_gfx_bggpu_testgl.h',
    'tools/profiling/native/sl_gfx_bggpu_testgl.c',
    'tools/profiling/native/test_sl_gfx_bggpu.c',
    'tools/windows/perftest.ps1', 'tools/windows/skybatchtest.ps1',
    'tools/windows/bggputest.ps1', 'tools/windows/perfchecks.ps1')) {
    $pin = Get-Pin (Join-Path $repo $rel)
    $pin.rel = $rel
    $sourcePins += $pin
}

$buildPins = @()
foreach ($rel in @('build/win32/sightline.exe', 'build/win32/SDL2.dll',
                   'build/win32/libwinpthread-1.dll')) {
    $pin = Get-Pin (Join-Path $repo $rel)
    $pin.rel = $rel
    $buildPins += $pin
}

# ---- run -----------------------------------------------------------------

$results = @()
foreach ($row in $rows) {
    $full = Join-Path $repo $row.script
    $o = Join-Path $Out ($row.name + '.out.log')
    $e = Join-Path $Out ($row.name + '.err.log')
    $argv = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $full)
    if ($Msys2Root) { $argv += @('-Msys2Root', $Msys2Root) }
    $start = (Get-Date).ToUniversalTime().ToString('o')
    $code = $null
    $err = ''
    if (-not (Test-Path -LiteralPath $full -PathType Leaf)) {
        $err = "the entrypoint does not exist: $full"
    } else {
        try {
            $p = Start-Process -FilePath 'powershell.exe' -ArgumentList $argv `
                               -WorkingDirectory $repo -NoNewWindow -PassThru `
                               -RedirectStandardOutput $o -RedirectStandardError $e
            # Cache the handle BEFORE waiting: without it .ExitCode can read
            # back $null once the process object has been released.
            $null = $p.Handle
            $p.WaitForExit()
            $code = $p.ExitCode
        } catch {
            $err = $_.Exception.Message
        }
    }
    $end = (Get-Date).ToUniversalTime().ToString('o')

    if ($null -eq $code) { $status = 'FAIL' }
    elseif ($code -eq 0) { $status = 'PASS' }
    else                 { $status = 'FAIL' }

    # The excerpt is for a reader deciding WHETHER to open the log, so it is
    # the failing lines when there are any and the tail otherwise - never a
    # summary, and never a substitute for the raw file.
    # EVERY ELEMENT IS FORCED TO A STRING, AND EVERY STRING IS BOUNDED.
    #
    # MEASURED: this table was written at 50 MB for one failing row - 199727
    # lines of .NET type metadata (`DefinedTypes`, `CodeBase`, `EntryPoint`:
    # an Assembly object's graph), against the 321 KB the same table takes
    # when every row is a string. ConvertTo-Json serialises whatever object it
    # is handed, so a single non-string reaching this array walks its whole
    # type graph and the evidence file becomes unreadable and unusable as
    # machine evidence - which is the one thing it exists to be.
    #
    # Rather than find which value leaked and trust it never happens again,
    # the array is made incapable of holding anything but a bounded string:
    # [string] on every element, 400 characters each, 20 elements. A caller
    # that needs more reads the raw log, whose path is in this same row.
    $excerpt = @()
    if ($status -eq 'FAIL') {
        $raw = @()
        if ($err) { $raw += $err }
        foreach ($f in @($o, $e)) {
            if (Test-Path -LiteralPath $f) {
                $lines = @(Get-Content -LiteralPath $f -ErrorAction SilentlyContinue)
                $hit = @($lines | Where-Object { $_ -match 'FAIL|error|Error|refus|cannot|Exception' })
                if ($hit.Count -gt 0) { $raw += @($hit | Select-Object -First 12) }
                elseif ($lines.Count -gt 0) { $raw += @($lines | Select-Object -Last 8) }
            }
        }
        foreach ($x in @($raw | Select-Object -First 20)) {
            $s = [string]$x
            if ($s.Length -gt 400) { $s = $s.Substring(0, 400) + ' ...[truncated]' }
            $excerpt += $s
        }
    }

    $results += [ordered]@{
        name = $row.name; status = $status; exit_code = $code
        command = "powershell.exe $($argv -join ' ')"
        cwd = $repo; started_utc = $start; ended_utc = $end
        stdout_log = $o; stderr_log = $e
        covers = $row.covers
        excerpt = $excerpt
    }
    Write-Host ("{0,-14} {1,-5} exit {2}" -f $row.name, $status, $code)
}

foreach ($nc in $notcovered) {
    $results += [ordered]@{
        name = $nc.name; status = 'NOT_COVERED'; exit_code = $null
        command = $null; cwd = $null; started_utc = $null; ended_utc = $null
        stdout_log = $null; stderr_log = $null
        covers = $nc.why; needs = $nc.needs; excerpt = @()
    }
    Write-Host ("{0,-14} {1,-5}" -f $nc.name, 'NOT_COVERED')
}

$failed = @($results | Where-Object { $_.status -eq 'FAIL' })

$doc = [ordered]@{
    schema = 'sightline-perfchecks/1'
    generated_utc = (Get-Date).ToUniversalTime().ToString('o')
    repo = $repo
    rows_run = @($rows | ForEach-Object { $_.name })
    counts = [ordered]@{
        pass = @($results | Where-Object { $_.status -eq 'PASS' }).Count
        fail = $failed.Count
        not_covered = @($results | Where-Object { $_.status -eq 'NOT_COVERED' }).Count
    }
    source_pins = $sourcePins
    build_pins = $buildPins
    results = $results
}
$json = Join-Path $Out 'result.json'
# NO BYTE ORDER MARK. PowerShell 5.1's `Out-File -Encoding utf8` writes one,
# and a BOM at the head of a JSON document is not whitespace: a strict parser
# refuses it (Python's json.load raises "Unexpected UTF-8 BOM" unless the
# caller already knows to decode as utf-8-sig). This table is machine
# evidence, so it is written through UTF8Encoding($false), which emits no
# preamble. The same applies to the Markdown below, for consistency.
$noBom = New-Object System.Text.UTF8Encoding($false)
[IO.File]::WriteAllText($json, ($doc | ConvertTo-Json -Depth 8), $noBom)

$md = @()
$md += '# OPTIMIZED RENDERING: CPU check batch'
$md += ''
$md += ('Generated ' + $doc.generated_utc + ' - PASS ' + $doc.counts.pass +
        ', FAIL ' + $doc.counts.fail + ', NOT_COVERED ' + $doc.counts.not_covered)
$md += ''
$md += '| row | status | exit | evidence |'
$md += '|---|---|---|---|'
foreach ($r in $results) {
    $ec = $r.exit_code
    if ($null -eq $ec) { $ec = '-' }
    $ev = $r.stdout_log
    if ($null -eq $ev) { $ev = 'needs: ' + $r.needs }
    $md += ('| ' + $r.name + ' | ' + $r.status + ' | ' + $ec + ' | ' + $ev + ' |')
}
if ($failed.Count -gt 0) {
    $md += ''
    $md += '## Failures'
    foreach ($r in $failed) {
        $md += ''
        $md += ('### ' + $r.name + ' (exit ' + $r.exit_code + ')')
        $md += '```'
        $md += $r.excerpt
        $md += '```'
    }
}
$md += ''
$md += '## What each row covers'
foreach ($r in $results) {
    $md += ''
    $md += ('- **' + $r.name + '** (' + $r.status + '): ' + $r.covers)
    if ($r.needs) { $md += ('  - to establish it: ' + $r.needs) }
}
[IO.File]::WriteAllText((Join-Path $Out 'result.md'), (($md -join "`n") + "`n"), $noBom)

Write-Host ''
Write-Host ("perfchecks: PASS $($doc.counts.pass) FAIL $($doc.counts.fail) NOT_COVERED $($doc.counts.not_covered)")
Write-Host ("perfchecks: table $json")
if ($failed.Count -gt 0) { exit 1 }
exit 0
