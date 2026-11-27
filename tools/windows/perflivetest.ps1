<#
.SYNOPSIS
    The OPTIMIZED RENDERING live batch's NEGATIVE-FIXTURE suite: drive
    perflive-check.py and the non-launching paths of perflive.ps1 against
    synthetic fake-child evidence. No GPU, no window, no driver, no ROM, no
    game, no capture, no replay.

.DESCRIPTION
    Run from an ordinary Windows PowerShell prompt at the repository root:

        .\tools\windows\perflivetest.ps1

    tools/windows/perflive-fixture.py fabricates the files a real batch leaves
    behind - an attempt directory with arms.json, one stderr per arm written in
    the engine's own format strings, and PPM captures with real headers and
    real payload lengths. This script drives the checker and the runner's
    non-launching paths against each fixture and asserts the OUTCOME, not just
    a failure: FAIL, NOT_COVERED, INCOMPLETE and PASS are four different things
    and a fixture that collapses them is not a test.

    EVERY NEGATIVE FIXTURE MUST BITE. For each one the suite also runs the
    fixture module's `legacy` rule - a re-implementation of the DEFECTIVE rule
    that fixture was built against - and requires it to say PASS. A fixture the
    old behaviour already rejected pins nothing, and is reported as such rather
    than counted. A fixture whose legacy rule is N/A is one that pins a new
    coverage requirement with no old counterpart; those are listed separately
    and are not counted as regression tests.

    WHAT IS CHECKED

      the checker's verdicts   one fixture per defect, each asserting the
                               status, the full-qualification flag and the
                               named witness that must fire
      the checker's gates      the manual outcomes in their three readings
                               (recorded FAIL, absent or unpinned, pinned
                               PASS), every mandatory CASE rather than one arm
                               per phase, the declared capture SCHEDULE
                               including two halves that agree on the wrong
                               moments, the performance row bound to this build
                               and recomputed from its own raw runs, and the
                               vertex helper's split net of the upper ranges it
                               recomputed
      the manual precedence    the three readings are ORDERED: a step recorded
                               FAIL outranks a step that was never recorded,
                               and does not waive it - the batch reads FAIL
                               while the absence is still reported
      the row, figure by       no field of the performance row is evidence of
      figure                   itself. A figure that is not a finite positive
                               time (a NaN, an infinity, a negative, a string,
                               or a JSON `true`, which a naive numeric test
                               admits), a sample bound to another build, arm or
                               stage, and an aggregated arm the row holds no
                               sample of each leave the row UNQUALIFIED; a
                               stated verdict its own recomputed samples
                               contradict FAILS it
      the exact-image rule     read from the checker's source: a capture pair is
                               compared by full sha256 with no tolerance, so a
                               one-pixel, one-channel difference is a FAIL, and
                               no tolerance term exists anywhere else in it
      the selector             a discovery failure writes selection-failed.json,
                               exits nonzero and writes NO selection; a
                               candidate whose counters read far-positive but
                               whose run did not exit zero, stay live or confirm
                               its assets is INELIGIBLE; a role no candidate
                               proved is refused in the selection rather than
                               filled with the far candidate
      the runner, not running  a stale attempt directory is refused; a mark of
                               another stage refuses to bind, in the discovery
                               phase as well as the dependent ones; -DryRun
                               prints the whole batch and starts nothing
      the receipt order        by reading the runner's own source: the intent is
                               written before the launch, the launch receipt
                               before any dependent step, and the exit receipt
                               from a finally block

    WHAT IS NOT. Nothing here measures the renderer, the GPU, a frame time or a
    pixel. Every byte of evidence it reads was fabricated by the fixture module.
    It establishes that the checker's rules hold on known-bad evidence; it
    establishes nothing about the build.

    Exits 0 when every assertion passes, 1 otherwise.

.PARAMETER Work
    Where to build the fixtures. Default a fresh directory under the system
    temp. It must be empty or absent. The tree is never deleted: a failed
    assertion is only diagnosable against the evidence it was read from.
#>
[CmdletBinding()]
param(
    [string]$Work
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$fixture = Join-Path $PSScriptRoot 'perflive-fixture.py'
$checker = Join-Path $PSScriptRoot 'perflive-check.py'
$runner  = Join-Path $PSScriptRoot 'perflive.ps1'

function Resolve-Python {
    if ($env:SL_PYTHON) {
        if (Test-Path -LiteralPath $env:SL_PYTHON) { return (Resolve-Path -LiteralPath $env:SL_PYTHON).Path }
    }
    $local = Join-Path $repo '.venv\Scripts\python.exe'
    if (Test-Path -LiteralPath $local) { return $local }
    $sibling = Join-Path (Split-Path -Parent $repo) 'sightline\.venv\Scripts\python.exe'
    if (Test-Path -LiteralPath $sibling) { return $sibling }
    return $null
}
$python = Resolve-Python
if (-not $python) { Write-Host 'perflivetest: no interpreter (SL_PYTHON, .\.venv, sibling .venv).'; exit 1 }
foreach ($f in @($fixture, $checker, $runner)) {
    if (-not (Test-Path -LiteralPath $f)) { Write-Host "perflivetest: missing $f"; exit 1 }
}

if (-not $Work) {
    $Work = Join-Path $env:TEMP ('perflivetest-' + (Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss'))
}
if (Test-Path -LiteralPath $Work) {
    $n = @(Get-ChildItem -LiteralPath $Work -Force -ErrorAction SilentlyContinue)
    if ($n.Count -gt 0) { Write-Host "perflivetest: '$Work' is not empty."; exit 1 }
}
New-Item -ItemType Directory -Force -Path $Work | Out-Null

$script:pass = 0
$script:fail = 0
$script:notes = @()

function Ck([string]$what, $ok, [string]$detail) {
    # THE VALUE IS COERCED HERE RATHER THAN BY THE PARAMETER'S TYPE. `-match`
    # over a collection returns the MATCHING ELEMENTS, so an assertion whose
    # left operand is an empty pipeline yields an empty array and not $false. A
    # [bool] parameter refuses that with a terminating binding error, which
    # ABORTS the suite at that assertion instead of reporting it as failed -
    # measured when the runner produced no arm lines at all: the run died there
    # and every later section, including the receipt order and the checker's
    # usage guards, was never reached. A test that cannot report a failure is
    # worth less than the failure. Booleans pass through unchanged, so every
    # assertion that already read true or false reads the same.
    if (-not ($ok -is [bool])) {
        $items = @($ok)
        if ($items.Count -eq 0) { $ok = $false }
        elseif ($items.Count -eq 1) { $ok = [bool]$items[0] }
        else { $ok = $true }
    }
    if ($ok) {
        $script:pass++
        Write-Host ("  ok    {0}" -f $what)
    } else {
        $script:fail++
        Write-Host ("  FAIL  {0}" -f $what)
        if ($detail) { Write-Host ("        {0}" -f $detail) }
    }
}

function Run-Py([string[]]$cargs) {
    # cmd owns the redirect: PowerShell 5.1 turns a native command's stderr
    # into error records, which $ErrorActionPreference='Stop' then throws on.
    $q = @("`"$python`"", '-I', '-B')
    foreach ($a in $cargs) { $q += ("`"" + $a + "`"") }
    $o = & cmd /c (($q -join ' ') + ' 2>&1')
    return @{ out = (@($o) -join "`n"); exit = $LASTEXITCODE }
}

function Run-Runner([string[]]$cargs) {
    $q = @('powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$runner`"")
    foreach ($a in $cargs) { $q += ("`"" + $a + "`"") }
    $o = & cmd /c (($q -join ' ') + ' 2>&1')
    return @{ out = (@($o) -join "`n"); exit = $LASTEXITCODE }
}

# ------------------------------------------------------- the fixture catalogue
$cat = Run-Py @($fixture, 'list')
if ($cat.exit -ne 0) { Write-Host "perflivetest: the fixture module would not list ($($cat.exit))"; exit 1 }
$names = @()
foreach ($line in ($cat.out -split "`n")) {
    $t = $line.Trim()
    if ($t -eq '') { continue }
    $names += ($t -split '\s+')[0]
}
Write-Host ''
Write-Host ("perflivetest: {0} fixtures" -f $names.Count)

# The fixtures the checker is run against; the rest are driven by the runner or
# by the selector below.
$SELECTOR_FIXTURES = @('d7-discovery-failure', 'd8-counters-only-selection')
# Selector fixtures where the FAR role is proved and a dependent role is not:
# a selection is written, and the unproved role must be refused in it rather
# than filled with the far candidate.
$SELECTOR_PARTIAL_FIXTURES = @('d14-selection-reduction-unproved')
$RUNNER_FIXTURES = @('d7-mark-wrong-stage', 'd8-stale-attempt')

# ------------------------------------------------------ 1. the checker's rules
Write-Host ''
Write-Host '--- the checker''s verdicts on fabricated evidence ---'
foreach ($n in $names) {
    if ($SELECTOR_FIXTURES -contains $n) { continue }
    if ($SELECTOR_PARTIAL_FIXTURES -contains $n) { continue }
    if ($RUNNER_FIXTURES -contains $n) { continue }
    $dir = Join-Path $Work $n
    $b = Run-Py @($fixture, 'build', $n, $dir)
    if ($b.exit -ne 0) { Ck "$n builds" $false $b.out; continue }
    $e = Run-Py @($fixture, 'expect', $n)
    if ($e.exit -ne 0) { Ck "$n declares an expectation" $false $e.out; continue }
    $exp = $e.out | ConvertFrom-Json

    $r = Run-Py @($checker, $dir)
    $cj = Join-Path $dir 'check.json'
    if (-not (Test-Path -LiteralPath $cj)) { Ck "${n}: the checker wrote check.json" $false $r.out; continue }
    $chk = Get-Content -LiteralPath $cj -Raw | ConvertFrom-Json

    Ck ("{0}: status {1}" -f $n, $exp.status) ("$($chk.status)" -eq "$($exp.status)") `
       ("read '$($chk.status)', expected '$($exp.status)'")

    if ($null -ne $exp.full_qualification) {
        $want = [bool]$exp.full_qualification
        Ck ("{0}: full_qualification {1}" -f $n, $want) ([bool]$chk.full_qualification -eq $want) `
           ("read '$($chk.full_qualification)'")
    }
    # SUBSET MUST NEVER CLAIM FULL ACCEPTANCE, whatever its status.
    if ("$($chk.scope)" -eq 'subset') {
        Ck ("{0}: a subset says so in words and claims no acceptance" -f $n) `
           (("$($chk.acceptance)" -match 'SUBSET') -and (-not [bool]$chk.full_qualification)) `
           ("acceptance='$($chk.acceptance)' full_qualification='$($chk.full_qualification)'")
    }

    # WHICH ASSERTION FIRES. A fixture that fails for some other reason is not
    # testing what it says it is.
    if ($exp.witness) {
        $fired = @()
        foreach ($a in $chk.arms) {
            foreach ($w in $a.witnesses) {
                if ("$($w.verdict)" -ne 'PASS') { $fired += "$($w.name)" }
            }
        }
        foreach ($w in @($chk.batch_witnesses) + @($chk.qualification_gates)) {
            if ($w -and "$($w.verdict)" -ne 'PASS') { $fired += "$($w.name)" }
        }
        Ck ("{0}: {1} is the assertion that fires" -f $n, $exp.witness) `
           ($fired -contains "$($exp.witness)") ("fired: " + ($fired -join ', '))
    } else {
        $fired = @()
        foreach ($a in $chk.arms) {
            foreach ($w in $a.witnesses) {
                if ("$($w.verdict)" -ne 'PASS') { $fired += "$($a.name)/$($w.name)=$($w.verdict)" }
            }
        }
        Ck ("{0}: nothing fires" -f $n) ($fired.Count -eq 0) ($fired -join ', ')
    }

    # DOES IT BITE? The defective rule this fixture was built against must have
    # accepted it, or the fixture pins nothing.
    $lg = Run-Py @($fixture, 'legacy', $n, $dir)
    if ($lg.exit -ne 0) {
        Ck ("{0}: the legacy rule runs" -f $n) $false $lg.out
    } else {
        $lj = $lg.out | ConvertFrom-Json
        $want = "$($exp.legacy_expected)"
        if ($want -eq 'N/A') {
            $script:notes += ("{0}: no old counterpart - {1}" -f $n, $lj.rule)
            Ck ("{0}: declared N/A against the old behaviour, and says so" -f $n) `
               ("$($lj.legacy_verdict)" -eq 'N/A') ("read '$($lj.legacy_verdict)'")
        } else {
            Ck ("{0}: the OLD rule accepted it (so this bites)" -f $n) `
               ("$($lj.legacy_verdict)" -eq 'PASS') `
               ("legacy said '$($lj.legacy_verdict)': $($lj.rule)")
        }
    }
}

# --------------- 1b. the manual precedence and the row's recomputed figures
# Two readings the loop above can only see as a batch status. A status says the
# batch failed; it does not say which rule failed it, nor that the rule read
# what it claims to read, so each is asserted on the NAMED gate's verdict and
# on what that gate's own detail says.
#
#   the manual precedence   a step recorded FAIL outranks a step that was never
#                           recorded, and does not waive it: the FAIL witness
#                           reads FAIL while the absence is still reported
#   the performance row     every figure the row states is recomputed from the
#                           row's own raw per-arm samples - a figure that is not
#                           a finite positive time, a sample bound to another
#                           build, arm or stage, and an arm the row aggregates
#                           without holding a sample of it all leave the row
#                           UNQUALIFIED, and a stated verdict the samples
#                           contradict FAILS it
Write-Host ''
Write-Host '--- the manual precedence, and the performance row figure by figure ---'

function Get-Gate([string]$fixtureName, [string]$gateName) {
    $cj = Join-Path (Join-Path $Work $fixtureName) 'check.json'
    if (-not (Test-Path -LiteralPath $cj)) { return $null }
    $chk = Get-Content -LiteralPath $cj -Raw | ConvertFrom-Json
    foreach ($g in @($chk.qualification_gates)) {
        if ("$($g.name)" -eq $gateName) { return $g }
    }
    return $null
}

# A RECORDED FAIL IS A MEASUREMENT; A MISSING STEP IS THE ABSENCE OF ONE. Both
# are present in this fixture, and the three assertions are three claims: the
# measurement is reported as a failure, the absence is still reported, and
# neither reading hides the other.
$fn = 'd18-manual-fail-outranks-absence'
$gp = Get-Gate $fn 'MANUAL_OUTCOMES_PASSED'
$gr = Get-Gate $fn 'MANUAL_OUTCOMES_RECORDED'
Ck ("{0}: the recorded FAIL reads FAIL, not NOT_COVERED" -f $fn) `
   (($null -ne $gp) -and ("$($gp.verdict)" -eq 'FAIL')) ("read '$($gp.verdict)'")
Ck ("{0}: the unrecorded step is still reported, so the FAIL waives no evidence" -f $fn) `
   (($null -ne $gr) -and ("$($gr.verdict)" -eq 'NOT_COVERED') -and ("$($gr.detail)" -match 'H6')) `
   ("$($gr.verdict): $($gr.detail)")
Ck ("{0}: the FAIL names the step that failed AND the step that is missing" -f $fn) `
   (($null -ne $gp) -and ("$($gp.excerpt)" -match 'H3') -and ("$($gp.detail)" -match 'H6')) `
   ("detail='$($gp.detail)' excerpt='$($gp.excerpt)'")

# A FIGURE THAT IS NOT A TIME IS NOT A MEASUREMENT. Five figures, five
# assertions: a status alone cannot say that each was rejected rather than one
# of them masking the rest. The JSON boolean is the one a naive numeric test
# admits, because isinstance(True, int) is true in Python.
$fn = 'd19-performance-raw-malformed'
$g = Get-Gate $fn 'PERFORMANCE_ROW_PRESENT'
Ck ("{0}: the row is UNQUALIFIED - not failed, and not passed" -f $fn) `
   (($null -ne $g) -and ("$($g.verdict)" -eq 'NOT_COVERED')) ("read '$($g.verdict)'")
$dt = "$($g.detail)"
$bad = [ordered]@{
    'a NaN'               = 'mean_ms is NaN'
    'an infinity'         = 'p99_ms_nearest_rank is inf, which is not finite'
    'a negative duration' = 'mean_ms is -11.2, which is not a positive duration'
    'a string'            = 'mean_ms is str, not a number'
    'a JSON boolean'      = 'p99_ms_nearest_rank is the JSON boolean True, not a time'
}
foreach ($k in $bad.Keys) {
    Ck ("{0}: {1} where a frame time belongs is rejected by name" -f $fn, $k) `
       ($dt.Contains($bad[$k])) $dt
}

# A MEAN BESIDE A ROW IS NOT A MEASUREMENT OF IT. Three bindings, three
# assertions: the build the run was taken on, the arm it measured and the stage
# that arm ran.
$fn = 'd20-performance-raw-unbound'
$g = Get-Gate $fn 'PERFORMANCE_ROW_PRESENT'
Ck ("{0}: the row is UNQUALIFIED - not failed, and not passed" -f $fn) `
   (($null -ne $g) -and ("$($g.verdict)" -eq 'NOT_COVERED')) ("read '$($g.verdict)'")
$dt = "$($g.detail)"
Ck ("{0}: the sample pinned to another build is refused" -f $fn) `
   ($dt.Contains('is pinned to build')) $dt
Ck ("{0}: the sample naming an arm the batch never ran is refused" -f $fn) `
   ($dt.Contains("names arm 'X-depth-on', which this batch did not run")) $dt
Ck ("{0}: the sample declaring a stage its arm did not run is refused" -f $fn) `
   ($dt.Contains("declares stage 'silo' while arm 'B-depth-off' ran on 'runway'")) $dt

$fn = 'd21-performance-arm-unsampled'
$g = Get-Gate $fn 'PERFORMANCE_ROW_PRESENT'
Ck ("{0}: the row is UNQUALIFIED - not failed, and not passed" -f $fn) `
   (($null -ne $g) -and ("$($g.verdict)" -eq 'NOT_COVERED')) ("read '$($g.verdict)'")
Ck ("{0}: the aggregated arm that holds no sample of its own is named" -f $fn) `
   ("$($g.detail)".Contains("aggregates arm 'B-depth-on' and carries no raw measurement of it")) `
   ("$($g.detail)")

# THE BUDGET VERDICT IS DERIVED FROM THE SAMPLES, NEVER READ OFF THE ROW. The
# row asserts the budget was met; its own nearest-rank p99 says otherwise, and
# READINESS.md:45-47 reads BOTH mean and p99, which is why a row whose means
# are both under budget still fails.
$fn = 'd22-performance-false-budget'
$g = Get-Gate $fn 'PERFORMANCE_ROW_PRESENT'
Ck ("{0}: a stated verdict its own samples contradict FAILS the row" -f $fn) `
   (($null -ne $g) -and ("$($g.verdict)" -eq 'FAIL')) ("read '$($g.verdict)'")
$ex = "$($g.excerpt)"
Ck ("{0}: budget_met is recomputed, and the recomputation is what is reported" -f $fn) `
   ($ex.Contains('the row states budget_met=True') -and
    $ex.Contains('recomputed from its own 2 raw run(s) that is False')) $ex
Ck ("{0}: the run over budget is named with its own p99 against the 16.667 ms bar" -f $fn) `
   ($ex.Contains('24.9') -and $ex.Contains('16.667')) $ex

# THE POSITIVE CONTROL MUST STILL QUALIFY. Rules that only ever refuse are not
# rules: the healthy row passes with every figure recomputed, and says so.
$g = Get-Gate 'pass-full' 'PERFORMANCE_ROW_PRESENT'
Ck 'pass-full: the healthy row still qualifies with every stated figure recomputed' `
   (($null -ne $g) -and ("$($g.verdict)" -eq 'PASS') -and
    ("$($g.detail)" -match 'recomputed here from those runs')) ("$($g.verdict): $($g.detail)")
$g = Get-Gate 'pass-full' 'MANUAL_OUTCOMES_PASSED'
Ck 'pass-full: every manual step is recorded, pinned and still reads PASS' `
   (($null -ne $g) -and ("$($g.verdict)" -eq 'PASS')) ("read '$($g.verdict)'")

# ---------- 1c. WHY a #95 witness read what it read -------------------------
# A status and a witness NAME cannot separate these readings: a log refused
# because the parser lost the grammar and a log refused because the driver
# refused the program fail the same witness with the same verdict. The two
# helper defects corrected this round were both of that shape - a healthy run
# misread as a parser fault, and an unmeasured window misread as a measured
# one - so each is asserted on the DETAIL the witness reports, which is the
# only place the difference is written down.
function Get-ArmWitness([string]$fixtureName, [string]$armName, [string]$wName) {
    $cj = Join-Path (Join-Path $Work $fixtureName) 'check.json'
    if (-not (Test-Path -LiteralPath $cj)) { return $null }
    $chk = Get-Content -LiteralPath $cj -Raw | ConvertFrom-Json
    foreach ($a in @($chk.arms)) {
        if ("$($a.name)" -ne $armName) { continue }
        foreach ($w in @($a.witnesses)) {
            if ("$($w.name)" -eq $wName) { return $w }
        }
    }
    return $null
}

# Every witness detail and excerpt in one fixture's check.json, so that "the
# parser fault is reported NOWHERE in this reading" is a claim about the whole
# reading rather than about one witness that happened to be looked at.
function Get-AllWitnessText([string]$fixtureName) {
    $cj = Join-Path (Join-Path $Work $fixtureName) 'check.json'
    if (-not (Test-Path -LiteralPath $cj)) { return '' }
    $chk = Get-Content -LiteralPath $cj -Raw | ConvertFrom-Json
    $t = ''
    foreach ($a in @($chk.arms)) {
        foreach ($w in @($a.witnesses)) { $t += "$($w.name)|$($w.verdict)|$($w.detail)|$($w.excerpt)`n" }
    }
    foreach ($w in @($chk.batch_witnesses) + @($chk.qualification_gates)) {
        if ($w) { $t += "$($w.name)|$($w.verdict)|$($w.detail)|$($w.excerpt)`n" }
    }
    return $t
}

Write-Host ''
Write-Host '--- the #95 diagnostics, and the origin of the capture bracket ---'

# THE MODULE'S OWN DIAGNOSTICS ARE NOT CENSUSES. Each of these logs carries at
# least one line with the census prefix that can never match the census
# grammar, and in none of them may the reading be a parser fault.
foreach ($fn in @('tf-ready-banner-pass', 'tf-plural-forms-pass',
                  'tf-banner-only-no-census', 'tf-unavailable-diagnostic',
                  'tf-program-refused-colon-form',
                  'tf-off-arm-unrecognised-switch-value')) {
    $t = Get-AllWitnessText $fn
    Ck ("{0}: no witness reports a MALFORMED #95 line" -f $fn) `
       (($t -ne '') -and (-not ($t -match 'did not parse'))) `
       (($t -split "`n" | Where-Object { $_ -match 'did not parse' }) -join ' || ')
}

# THE READY BANNER A WORKING CANDIDATE PRINTS. The positive controls carry the
# actual emitted banner, so these two assertions are what say the corrected
# classification accepts the one log shape a successful live start produces.
foreach ($fn in @('tf-ready-banner-pass', 'tf-plural-forms-pass')) {
    $w = Get-ArmWitness $fn 'TF_ON' 'TF_PREPASSES_POSITIVE'
    Ck ("{0}: the candidate's own counters are read over the closed bracket" -f $fn) `
       (($null -ne $w) -and ("$($w.verdict)" -eq 'PASS') -and
        ("$($w.detail)" -match 'over the capture-bracket')) `
       ("$($w.verdict): $($w.detail)")
}

# A BANNER WITH NO CENSUS IS AN ABSENCE. The banner is unconditional and the
# census line is census-branch only, so this is the shape of a run that armed
# no statistics reader - not a report that failed to parse.
$fn = 'tf-banner-only-no-census'
$w = Get-ArmWitness $fn 'TF_ON' 'TF_PREPASSES_POSITIVE'
Ck ("{0}: the reading is NOT_COVERED and names the absent census line" -f $fn) `
   (($null -ne $w) -and ("$($w.verdict)" -eq 'NOT_COVERED') -and
    ("$($w.detail)" -match 'printed no #95 clip-prepass line at all')) `
   ("$($w.verdict): $($w.detail)")

# A REFUSAL IS REPORTED AS A REFUSAL. The module's state field carries the
# latched reason, and the witness must repeat that reason rather than a
# grammar complaint - that is the difference between "the driver declined" and
# "the parser drifted".
$cases = [ordered]@{
    'tf-unavailable-diagnostic'     = 'transform feedback entry points are not all present'
    'tf-program-refused-colon-form' = 'the prepass program did not compile'
}
foreach ($fn in $cases.Keys) {
    $w = Get-ArmWitness $fn 'TF_ON' 'TF_STATE_READY'
    Ck ("{0}: the FAIL names the module's own refusal reason" -f $fn) `
       (($null -ne $w) -and ("$($w.verdict)" -eq 'FAIL') -and
        ("$($w.excerpt)".Contains($cases[$fn]))) `
       ("$($w.verdict): $($w.excerpt)")
}

# THE EXEMPTION SUBTRACTS THE KNOWN DIAGNOSTICS AND NO MORE. A drifted census
# in the same log as the real banner must still be MALFORMED; an exemption
# applied as a count rather than as a pattern would have swallowed the drift.
$fn = 'tf-malformed-census-beside-the-banner'
$w = Get-ArmWitness $fn 'TF_ON' 'TF_PREPASSES_POSITIVE'
Ck ("{0}: the drift is still reported as a MALFORMED line, beside the banner" -f $fn) `
   (($null -ne $w) -and ("$($w.verdict)" -eq 'FAIL') -and
    ("$($w.detail)" -match 'did not parse')) ("$($w.verdict): $($w.detail)")
Ck ("{0}: exactly the drifted lines are counted, not the banner" -f $fn) `
   (($null -ne $w) -and ("$($w.excerpt)" -eq 'parsed=0 prefixes=4')) ("$($w.excerpt)")

# A MISSING BOUNDARY IS NO READING. Both halves: the origin, which this round
# stopped inventing, and the close, which was already required. Each must be
# refused BY NAME and must say that the weaker reading was not substituted.
$fn = 'tf-missing-pre-report'
$w = Get-ArmWitness $fn 'TF_ON' 'TF_PREPASSES_POSITIVE'
Ck ("{0}: no report before the first capture is NO READING, not a zero origin" -f $fn) `
   (($null -ne $w) -and ("$($w.verdict)" -eq 'NOT_COVERED') -and
    ("$($w.detail)" -match 'no #95 report PRECEDES') -and
    ("$($w.detail)" -match 'zero origin is NOT')) ("$($w.verdict): $($w.detail)")
$fn = 'tf-bracket-did-not-close'
$w = Get-ArmWitness $fn 'TF_ON' 'TF_PREPASSES_POSITIVE'
Ck ("{0}: an unclosed bracket is NO READING, and the totals are not substituted" -f $fn) `
   (($null -ne $w) -and ("$($w.verdict)" -eq 'NOT_COVERED') -and
    ("$($w.detail)" -match 'does not close') -and
    ("$($w.detail)" -match 'are NOT substituted')) ("$($w.verdict): $($w.detail)")

# A ZERO DELTA OVER LARGE TOTALS. The bracket closes, so a reading exists and
# it is zero. The totals are in the same fields of the same log, which is what
# makes this the case a fallback would pass: the excerpt must show that the
# reading had them to hand and reported zero anyway.
$fn = 'tf-zero-delta-large-totals'
$w = Get-ArmWitness $fn 'TF_ON' 'TF_PREPASSES_POSITIVE'
Ck ("{0}: the bracket reading is 0 and the verdict follows it, not the totals" -f $fn) `
   (($null -ne $w) -and ("$($w.verdict)" -eq 'FAIL') -and
    ("$($w.detail)" -match 'prepasses read 0 over the capture-bracket')) `
   ("$($w.verdict): $($w.detail)")
Ck ("{0}: the large whole-run totals are reported beside the zero reading" -f $fn) `
   (($null -ne $w) -and ("$($w.excerpt)" -match "'prepasses': 40") -and
    ("$($w.excerpt)" -match 'last=')) ("$($w.excerpt)")

# THE OFF ARM IS UNCHANGED. An unrecognised switch value prints a #95 line
# with a DIFFERENT prefix, which is neither a census nor one of the four
# exempted diagnostics, so no count moves for it and the arm reads zero TF
# work with the emitter's own off phrase - the same reading an absent switch
# gives.
$fn = 'tf-off-arm-unrecognised-switch-value'
$w = Get-ArmWitness $fn 'ORIG_CPU' 'TF_OFF_ARMS_READ_ZERO'
Ck ("{0}: zero prepasses AND the off phrase, read over the closed bracket" -f $fn) `
   (($null -ne $w) -and ("$($w.verdict)" -eq 'PASS') -and
    ("$($w.excerpt)" -match 'prepasses=0') -and
    ("$($w.excerpt)" -match 'off_phrase=True')) ("$($w.verdict): $($w.excerpt)")

# THE EXACT-IMAGE RULE IS UNTOUCHED, and is read from the checker's own source
# rather than inferred from a passing fixture. A pair of captures is compared
# by full sha256 with no tolerance and no sampled pixel, so a single-pixel,
# single-channel difference is a FAIL. A full-checker pass never implies a
# qualified game path, and nothing in the numeric rules above may widen this.
$csrc = [IO.File]::ReadAllText($checker)
Ck 'the capture pair is still compared by full hash, byte for byte, no tolerance' `
   (($csrc -match 'FRAMES_BYTE_IDENTICAL') -and
    ($csrc -match 'ha, hb = sha256\(a\), sha256\(b\)') -and
    ($csrc -match 'compared byte for byte, no tolerance')) ''
# The only tolerance the checker admits is the half-digit on the performance
# row's PRINTED aggregate. Every other occurrence of the word reads "no
# tolerance", so a tolerance added anywhere near the images fails this.
$tolerances = @([regex]::Matches($csrc, '(?i)[^\n]{0,60}tolerance[^\n]{0,20}'))
$strayTolerance = @($tolerances | Where-Object {
    (-not ($_.Value -match 'no tolerance')) -and
    (-not ($_.Value -match 'PERF_PRINT_TOLERANCE_MS'))
})
Ck 'the only tolerance in the checker is the performance row''s printed-digit one' `
   ($strayTolerance.Count -eq 0) (($strayTolerance | ForEach-Object { $_.Value }) -join ' || ')

# ------------------------------------------------------------ 2. the selector
Write-Host ''
Write-Host '--- the selector: discovery must fail closed ---'
foreach ($n in $SELECTOR_FIXTURES) {
    $dir = Join-Path $Work $n
    $b = Run-Py @($fixture, 'build', $n, $dir)
    if ($b.exit -ne 0) { Ck "$n builds" $false $b.out; continue }
    $r = Run-Py @($checker, '--select', $dir)
    Ck ("{0}: --select exits nonzero" -f $n) ($r.exit -ne 0) ("exit $($r.exit)")
    Ck ("{0}: NO selected.json is written" -f $n) `
       (-not (Test-Path -LiteralPath (Join-Path $dir 'selected.json'))) ''
    $sf = Join-Path $dir 'selection-failed.json'
    Ck ("{0}: selection-failed.json is written" -f $n) (Test-Path -LiteralPath $sf) ''
    if (Test-Path -LiteralPath $sf) {
        $sj = Get-Content -LiteralPath $sf -Raw | ConvertFrom-Json
        Ck ("{0}: it reads NOT_COVERED, not FAIL and not PASS" -f $n) `
           ("$($sj.status)" -eq 'NOT_COVERED') ("read '$($sj.status)'")
        if ($n -eq 'd8-counters-only-selection') {
            $c0 = @($sj.candidates)[0]
            Ck 'd8: the far-positive candidate is INELIGIBLE on its own run, not on its counters' `
               ((-not [bool]$c0.eligible) -and ([int]$c0.far0_calls -gt 0)) `
               ("eligible=$($c0.eligible) far0_calls=$($c0.far0_calls) because=$($c0.ineligible_because -join '; ')")
        }
    }
    $lg = Run-Py @($fixture, 'legacy', $n, $dir)
    if ($lg.exit -eq 0) {
        $lj = $lg.out | ConvertFrom-Json
        Ck ("{0}: the OLD rule accepted it (so this bites)" -f $n) `
           ("$($lj.legacy_verdict)" -eq 'PASS') ("legacy said '$($lj.legacy_verdict)'")
    } else {
        Ck ("{0}: the legacy rule runs" -f $n) $false $lg.out
    }
}

# --------------------------- 2b. the selector: an unproved role is refused
# A role with no evidence of its own is NOT_COVERED, and the far candidate is
# never substituted for it. The measured case this pins: a discovery run
# selected reduction = runway while that run's own fogless figure was null and
# its own reduction report read 0 reduced, refused for statistics.
Write-Host ''
Write-Host '--- the selector: an unproved role is refused, not substituted ---'
foreach ($n in $SELECTOR_PARTIAL_FIXTURES) {
    $dir = Join-Path $Work $n
    $b = Run-Py @($fixture, 'build', $n, $dir)
    if ($b.exit -ne 0) { Ck "$n builds" $false $b.out; continue }
    $r = Run-Py @($checker, '--select', $dir)
    Ck ("{0}: --select exits nonzero while a role is unselected" -f $n) ($r.exit -ne 0) ("exit $($r.exit)")
    $sj = Join-Path $dir 'selected.json'
    Ck ("{0}: a selection IS written, because the far role was proved" -f $n) `
       (Test-Path -LiteralPath $sj) $r.out
    if (Test-Path -LiteralPath $sj) {
        $s = Get-Content -LiteralPath $sj -Raw | ConvertFrom-Json
        Ck ("{0}: the selection reads INCOMPLETE, not SELECTED" -f $n) `
           ("$($s.status)" -eq 'INCOMPLETE') ("read '$($s.status)'")
        Ck ("{0}: the far role is selected on its own measurement" -f $n) `
           ((("$($s.far.stage)") -eq 'runway') -and ("$($s.far.status)" -eq 'SELECTED')) `
           ("far=$($s.far.stage)/$($s.far.status)")
        Ck ("{0}: the reduction role reads NOT_COVERED" -f $n) `
           ("$($s.reduction.status)" -eq 'NOT_COVERED') ("read '$($s.reduction.status)'")
        # The whole point: the far stage must NOT have been assigned to it.
        Ck ("{0}: the far candidate is NOT assigned to the reduction role" -f $n) `
           (($null -eq $s.reduction.stage) -and ("$($s.reduction.stage)" -ne "$($s.far.stage)")) `
           ("reduction stage='$($s.reduction.stage)' far stage='$($s.far.stage)'")
        Ck ("{0}: the unselected role is named in the record" -f $n) `
           (@($s.unselected_roles) -contains 'reduction') `
           ("unselected_roles=$(@($s.unselected_roles) -join ', ')")
        $c = @($s.candidates | Where-Object { $_.stage -eq 'runway' })[0]
        Ck ("{0}: the rejected candidate's own fogless figure is null and its reduction count 0" -f $n) `
           (($null -eq $c.fogless_frames) -and ([int]$c.reduced -eq 0)) `
           ("fogless_frames='$($c.fogless_frames)' reduced='$($c.reduced)'")
    }
    $lg = Run-Py @($fixture, 'legacy', $n, $dir)
    if ($lg.exit -eq 0) {
        $lj = $lg.out | ConvertFrom-Json
        Ck ("{0}: the OLD rule accepted it (so this bites)" -f $n) `
           ("$($lj.legacy_verdict)" -eq 'PASS') ("legacy said '$($lj.legacy_verdict)': $($lj.rule)")
    } else {
        Ck ("{0}: the legacy rule runs" -f $n) $false $lg.out
    }
}

# ------------------------------------- 3. the runner, on its non-launching paths
# A scratch environment the runner's preflight can satisfy without a build, a
# ROM or an install: a stand-in executable, a stand-in ROM and three empty asset
# roots. Nothing here is the player's, and nothing is launched - every runner
# invocation below is -DryRun or exits in preflight.
Write-Host ''
Write-Host '--- the runner, launching nothing ---'
$sandbox = Join-Path $Work '_sandbox'
New-Item -ItemType Directory -Force -Path (Join-Path $sandbox 'assets\worldassets') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $sandbox 'assets\texpacks') | Out-Null
$fakeRom = Join-Path $sandbox 'stand-in.z64'
[IO.File]::WriteAllBytes($fakeRom, (New-Object byte[] 64))
$assetDir = Join-Path $sandbox 'assets'
$waDir = Join-Path $assetDir 'worldassets'
$tpDir = Join-Path $assetDir 'texpacks'

$exePath = Join-Path $repo 'build\win32\sightline.exe'
$haveExe = Test-Path -LiteralPath $exePath
if (-not $haveExe) {
    Write-Host '  note: no build\win32\sightline.exe, so the runner cannot get past its own'
    Write-Host '        preflight. The runner assertions below are SKIPPED and reported as such.'
    $script:notes += ('the runner''s -DryRun assertions were SKIPPED: no build\win32\sightline.exe ' +
                      'to hash. They need a built executable and still launch nothing.')
}

$savedRom = $env:SL_ROM
$before = @(Get-Process -Name 'sightline' -ErrorAction SilentlyContinue).Count
try {
    $env:SL_ROM = $fakeRom

    if ($haveExe) {
        # 3a. A STALE ATTEMPT DIRECTORY IS REFUSED, before anything is built or
        #     launched. The fixture plants the artefacts that used to be read by
        #     name: a selection, a check and a capture from an earlier run.
        $n = 'd8-stale-attempt'
        $dir = Join-Path $Work $n
        $b = Run-Py @($fixture, 'build', $n, $dir)
        Ck "$n builds" ($b.exit -eq 0) $b.out
        $r = Run-Runner @('-DryRun', '-Out', $Work, '-Attempt', $n,
                          '-AssetOverrideDir', $assetDir, '-WorldAssetRoot', $waDir,
                          '-TexpackRoot', $tpDir)
        Ck "${n}: the runner refuses a non-empty attempt directory" `
           (($r.exit -eq 1) -and ($r.out -match 'is not empty')) ("exit $($r.exit)")
        Ck "${n}: it deletes nothing and launches nothing" `
           (($r.out -match 'Nothing is\s+deleted here') -or ($r.out -match 'nothing is\s+launched') -or ($r.out -match 'deleted here')) `
           $r.out
        Ck "${n}: the stale selection is still there, unread and unconsumed" `
           (Test-Path -LiteralPath (Join-Path $dir 'selected.json')) ''
        $lg = Run-Py @($fixture, 'legacy', $n, $dir)
        if ($lg.exit -eq 0) {
            $lj = $lg.out | ConvertFrom-Json
            Ck "${n}: the OLD rule accepted it (so this bites)" ("$($lj.legacy_verdict)" -eq 'PASS') `
               ("legacy said '$($lj.legacy_verdict)'")
        }

        # 3b. A MARK OF ANOTHER STAGE REFUSES TO BIND - in the discovery phase
        #     as well as the dependent ones. Every mark in this fixture is a
        #     legacy lvl33 (Dam) record, which is what the only two marks on
        #     this machine are, and Dam is the documented far-NEGATIVE control.
        #
        #     The contracts are unchanged; where they are carried is not. The
        #     optional poses now bind the cases the measured recipe does NOT
        #     qualify, and -Acquire is what builds the discovery arms, so the
        #     discovery-phase mark handling is driven with -Acquire given.
        $n = 'd7-mark-wrong-stage'
        $marks = Join-Path $Work $n
        $b = Run-Py @($fixture, 'build', $n, $marks)
        Ck "$n builds" ($b.exit -eq 0) $b.out
        $r = Run-Runner @('-DryRun', '-Out', (Join-Path $Work '_dry-marks'),
                          '-Marks', $marks, '-Acquire',
                          '-AssetOverrideDir', $assetDir, '-WorldAssetRoot', $waDir,
                          '-TexpackRoot', $tpDir)
        Ck "${n}: the poses that would close the unqualified cases are reported NOT BOUND" `
           ($r.out -match 'OPTIONAL POSES NOT BOUND \(the cases they would close stay NOT_COVERED\)') `
           $r.out
        Ck "${n}: the refusal names the stage mismatch and the mark's hash" `
           ($r.out -match 'needs a mark of stage \d+; far-blended\.txt is of stage 33 \(sha256 [0-9a-f]{64}\)') `
           $r.out
        # -Marks IS honoured in the discovery phase: New-AcqArms looks for
        # acq-<stage>.txt and binds it through the same Get-BoundPose. What it
        # must never do is bind one of another stage, and this fixture's
        # acq-silo.txt / acq-bunker1.txt are both stage-33 records.
        Ck "${n}: -Marks is honoured in the DISCOVERY phase too, and a wrong-stage mark does not bind" `
           (($r.out -match '\[ACQ-silo\] phase ACQ') -and
            (-not ($r.out -match 'mark sha256 [0-9a-f]{64}, stage 33')) -and
            ($r.out -match 'a probe at the start pad is declared as such rather than recorded as a pose')) `
           $r.out
        # LEFT FAILING ON PURPOSE - a contract the restructuring dropped.
        # Get-BoundPose records the discovery refusal into $script:poseRefusals
        # (perflive.ps1:600), but that list is reset at :1637 and PRINTED at
        # :1642-1645, while New-AcqArms does not run until :1655 - so a
        # discovery mark refused for being of another stage is appended after
        # the only place it would have been printed, and the probe's own pose
        # line then reads "no acq-silo.txt bound a pose" although one exists
        # and was refused. A reader of that line concludes no mark was there.
        # The dependent poses print their refusals correctly; this is the
        # discovery phase's alone, and it is a wording-and-ordering defect in
        # the runner rather than a stale assertion here.
        Ck "${n}: the discovery phase's own mark refusal is printed, naming the stage mismatch" `
           ($r.out -match 'ACQ-\w+ needs a mark of stage \d+; acq-\w+\.txt is of stage 33 \(sha256 [0-9a-f]{64}\)') `
           ('the refusal is recorded into $script:poseRefusals at perflive.ps1:600 but that list is ' +
            'printed at :1642 before New-AcqArms runs at :1655, so it is never printed; the probe ' +
            'instead reports "no acq-silo.txt bound a pose" for a mark that exists and was refused')
        # NO POSE FALLS BACK SILENTLY. Two readings, because a printed refusal
        # and an unbound arm are different claims: every refusal is printed,
        # and no arm anywhere in the batch binds a mark of the refused stage.
        Ck "${n}: every refusal is printed, and the cases they would close stay NOT_COVERED" `
           (($r.out -match 'needs a mark of stage \d+; \S+\.txt is of stage 33') -and
            ($r.out -match 'BLENDED_FAR_RANGE_GEOMETRY') -and ($r.out -match 'OCCLUSION_ORDERING')) `
           $r.out
        Ck "${n}: no arm binds a mark of the wrong stage instead" `
           (-not ($r.out -match 'mark sha256 [0-9a-f]{64}, stage 33')) $r.out
        Ck "${n}: a discovery probe at a start pad says so rather than recording a pose" `
           ($r.out -match 'a probe at the start pad is declared as such rather than recorded as a pose') `
           $r.out
        $lg = Run-Py @($fixture, 'legacy', $n, $marks)
        if ($lg.exit -eq 0) {
            $lj = $lg.out | ConvertFrom-Json
            Ck "${n}: the OLD rule accepted it (so this bites)" ("$($lj.legacy_verdict)" -eq 'PASS') `
               ("legacy said '$($lj.legacy_verdict)'")
        }

        # 3c. -DryRun PRINTS THE WHOLE BATCH AND STARTS NOTHING, and says in
        #     words that launching is the default.
        $r = Run-Runner @('-DryRun', '-Out', (Join-Path $Work '_dry-full'),
                          '-AssetOverrideDir', $assetDir, '-WorldAssetRoot', $waDir,
                          '-TexpackRoot', $tpDir)
        Ck '-DryRun: completes and says no child was started' `
           (($r.exit -eq 0) -and ($r.out -match 'DRY RUN COMPLETE\. No child process was started\.')) `
           ("exit $($r.exit)")
        # THE RUNWAY PAIR RUNS FIRST, AND IT IS A GATE. This is the contract the
        # measured far case buys: the one scene where the far path is measured
        # to submit is compared OFF against ON before anything else runs, and a
        # byte difference between the two halves stops the batch rather than
        # being worked around. Nothing pinned this before.
        Ck '-DryRun: the staged batch is smallest-first and the FAR-PAIR group is first' `
           (($r.out -match 'THE STAGED BATCH, SMALLEST-FIRST') -and
            ($r.out -match '(?s)THE STAGED BATCH, SMALLEST-FIRST.*?===\s*FAR-PAIR\s*===')) $r.out
        Ck '-DryRun: the first arm printed is the OFF half of the Runway pair' `
           (($r.out -split "`n" | Where-Object { $_ -match '^\[[A-Za-z0-9-]+\] phase ' } |
             Select-Object -First 1) -match '^\[B-depth-off\]') $r.out
        Ck '-DryRun: the far pair carries a GATE that stops and preserves on a difference' `
           ($r.out -match 'GATE: pair-images-identical - a failure here STOPS the batch and preserves') $r.out
        # THE GATE'S TEETH, read from the source: the dry run can only print
        # that the gate exists. What makes it a gate is that the comparison is
        # byte for byte with no tolerance, that a difference sets a stop which
        # BREAKS the schedule loop before any later group runs, and that the
        # comparison leaves its own receipt. Running the live mode here would
        # launch children, so this is asserted from the runner's text.
        $rsrc = [IO.File]::ReadAllText($runner)
        Ck 'the gate compares the two halves by hash with no tolerance' `
           (($rsrc -match 'function Test-PairIdentical') -and
            ($rsrc -match '\$ha = Get-Sha256Lower \$fa\[\$i\]\.FullName') -and
            ($rsrc -match 'byte for byte, no tolerance')) ''
        Ck 'a byte difference in the far pair stops the batch before any later group runs' `
           (($rsrc -match 'if \(\$script:gateStop\) \{ break \}') -and
            ($rsrc -match '\$script:gateStop = \[ordered\]@\{') -and
            ($rsrc -match 'STOP AND PRESERVE')) ''
        Ck 'the gate writes its own receipt, whichever way it reads' `
           ($rsrc -match "gate-far-pair\.json") ''
        # DISCOVERY is no longer the first phase: the far case is measured, so a
        # blanket probe is opt-in. The contract kept is that the runner still
        # DECLARES the discovery it is not doing, and says why - a silent
        # absence would be indistinguishable from having forgotten it.
        Ck '-DryRun: the acquisition phase is declared, off by default, with its reason' `
           (($r.out -match '--- ACQUISITION ---') -and
            ($r.out -match 'OFF \(-Acquire not given\)') -and
            ($r.out -match 'The far case is measured and needs no discovery')) $r.out
        Ck '-DryRun: each stage not probed is named with the measurement that retires it' `
           (($r.out -match 'NOT PROBED runway') -and ($r.out -match 'NOT PROBED surface') -and
            ($r.out -match 'already measured as a PANORAMA FALLBACK')) $r.out
        # A pose that is not bound does not re-pose an arm: it retires the case
        # it would have closed, in writing.
        Ck '-DryRun: an unbound optional pose leaves its case NOT_COVERED rather than falling back' `
           ($r.out -match 'OPTIONAL POSES NOT BOUND \(the cases they would close stay NOT_COVERED\)') $r.out
        # ROLES ARE REFUSED, NEVER SUBSTITUTED - the r526 defect, which filled
        # the reduction role with the far candidate.
        Ck '-DryRun: an unproved role is named UNPROVED and is not filled with the far candidate' `
           (($r.out -match '--- ROLES ---') -and ($r.out -match 'reduction\s+UNPROVED') -and
            ($r.out -match 'that is the fallback assignment this runner rejects by name')) $r.out
        Ck '-DryRun: prints the declared NOT_COVERED cases' `
           (($r.out -match 'DECLARED NOT_COVERED') -and ($r.out -match 'CLAMP_BRANCH_EXECUTED') -and
            ($r.out -match 'VERTEX_HELPER_SPLIT')) $r.out
        # THE SCRATCH CONFIG, pinned by its HASH rather than by its text. The
        # runner prints each arm's config path with the sha256 of the bytes it
        # will write, and 02ee9b33... is the digest of exactly
        # "version=1\npresentation=2\noptimized_rendering=1\n" - the bytes the
        # measured anchor record names. A hash is a stricter pin than a
        # substring match on a printed line, and it is the same value the
        # anchor, the runner and this assertion all independently carry.
        Ck '-DryRun: every arm pins the XBLA scratch config by the anchor''s own digest' `
           ($r.out -match 'config .*\(sha256 02ee9b337ebb1eb77e1a964a548963773fc990b20de2aa1b3e17c875816bd388\)') `
           $r.out
        # The initial save is the OTHER pinned byte set, and it must come from
        # the repository file, fresh per arm - the artifact folder's copy is the
        # POST-run save, which a read-write SL_EEPROM_RW rewrote in place.
        Ck '-DryRun: every arm takes a FRESH copy of the pinned initial save from the repository' `
           (($r.out -match 'fresh copy of .*tools\\trace\\inputs\\runway\.eeprom') -and
            ($r.out -match 'sha256 e6e9f1eaaa7968504c3bc9595ed51ff200bcf68f503492173192cf66e99808c8')) `
           $r.out
        Ck '-DryRun: every arm binds the three asset roots and the pinned RNG state' `
           (($r.out -match 'SL_WORLDASSET_ROOT=') -and ($r.out -match 'SL_TEXPACK_ROOT=') -and
            ($r.out -match 'SL_ASSET_OVERRIDE_DIR=') -and ($r.out -match 'SL_RNG_SEED=')) $r.out
        Ck '-DryRun: the movement arms carry the synthetic pad and its pinned step file' `
           (($r.out -match 'SL_PAD_VIRTUAL=xbox') -and ($r.out -match 'SL_PAD_SCRIPT=')) $r.out
        # Bounded to the arm's OWN printed block: a substring search over the
        # whole output would find a later census arm's variable and read as a
        # pass either way.
        $blocks = @{}
        $cur = $null
        foreach ($line in ($r.out -split "`n")) {
            if ($line -match '^\[([A-Za-z0-9-]+)\] phase ') { $cur = $Matches[1]; $blocks[$cur] = @() }
            elseif ($cur) { $blocks[$cur] += $line }
        }
        # A FEATURE IS NEVER MEASURED BY A SWITCH THAT DISABLES IT. The arms
        # that carry the combined-mode witnesses are the ones that must arm no
        # statistics reader: the census sets fstats, and the reduction's own
        # predicate refuses a walk while a statistics reader is active.
        #
        # These are now the G4-combined pair. G1-reduction and G2-skydome, which
        # carried this contract before, are no longer built unless their ROLE is
        # proved - and the runner refuses to fill either role with the far
        # candidate, which is the r526 defect. The contract is unchanged; the
        # arms that carry it are not the same arms.
        $censusFree = @('G4-combined-off', 'G4-combined-on')
        foreach ($an in $censusFree) {
            $blk = ($blocks[$an] -join "`n")
            Ck ("-DryRun: {0} arms no statistics reader (the switch that would disable what it measures)" -f $an) `
               (($blk) -and (-not ($blk -match 'SL_DL_CENSUS'))) $blk
            Ck ("-DryRun: {0} carries NO_CENSUS_REQUESTED as a witness" -f $an) `
               (($blk) -and ($blk -match 'NO_CENSUS_REQUESTED')) $blk
        }
        # THE FILL-WORKER ARM IS GONE, and its absence is pinned rather than
        # merely allowed. MEASURED at the one qualified far recipe: the fill
        # queries ran 181 batches of 0 predicted traces with 0 answers computed
        # by a worker, so an arm asserting FILL_WORKERS_DID_WORK there would
        # have declared a witness its own scene is measured not to produce. The
        # case stays open as FILL_QUERY_WORKERS.
        # Bounded to the arms' own DECLARATIONS - the `witnesses` line of each
        # printed block. A whole-output search reads the declared NOT_COVERED
        # prose too, which NAMES these witnesses in order to say why no arm
        # asks for them, so it would fail on the very text that documents the
        # retirement. What must hold is that no ARM declares them.
        $witLines = (($r.out -split "`n") | Where-Object { $_ -match '^\s+witnesses ' }) -join "`n"
        Ck '-DryRun: the arms declare witnesses at all (so the search below is not vacuous)' `
           (($witLines -match 'EXIT_ZERO') -and ($witLines -match 'CAPTURE_SCHEDULE_EXACT')) $witLines
        Ck '-DryRun: no arm claims the fill-query workers (measured: 181 batches of 0 predicted traces)' `
           (-not ($witLines -match 'FILL_WORKERS_DID_WORK')) $witLines
        Ck '-DryRun: the fill-query worker case is declared open instead' `
           ($r.out -match 'FILL_QUERY_WORKERS') $r.out
        # The same rule retires two more witnesses the measured run contradicts:
        # the single-range clamp submitted 0 calls / 0 indices at this recipe,
        # so no arm may claim it; and the vertex-helper split has no witness
        # here at all.
        Ck '-DryRun: no arm claims the single-range clamp (measured: 0 calls / 0 indices)' `
           ((-not ($witLines -match 'WINDOW_SINGLE_DELTA_POSITIVE')) -and
            (-not ($witLines -match 'CLAMP_INPUT_PRESENT'))) $witLines
        Ck '-DryRun: the single-range and clamp-branch cases are declared open instead' `
           (($r.out -match 'SINGLE_RANGE_SUBMISSION') -and ($r.out -match 'CLAMP_BRANCH_EXECUTED')) $r.out
        Ck '-DryRun: no arm claims the vertex-helper split, which cannot be witnessed here' `
           (-not ($witLines -match 'VERTEX_HELPER_SPLIT_POSITIVE')) $witLines
        # EVERY CAPTURING ARM DECLARES ITS FULL SCHEDULE, so the checker's
        # CAPTURE_SCHEDULE_EXACT has a grid to enforce and two identical WRONG
        # schedules cannot pass. E-pose2-* is omitted here deliberately: it is
        # built only under -Control, and asserting on an arm this invocation
        # does not build would pass vacuously.
        foreach ($an in @('B-depth-off', 'B-depth-on', 'C-guard', 'D-blend-off', 'D-blend-on',
                          'F-move-off', 'F-move-on')) {
            $blk = ($blocks[$an] -join "`n")
            Ck ("-DryRun: {0} declares its capture grid, its unit and its expected moments" -f $an) `
               (($blk) -and
                ($blk -match 'shots\s+gate=\w+ first=\d+ last=\d+ every=\d+ read_first=\d+ read_last=\d+') -and
                ($blk -match 'expected_moments = [\d, ]+\s+count=\d+\s+\d+x\d+')) $blk
        }
        # 3d. THE SECOND-POSE ARMS, which exist only under -Control. They are
        #     posed by a PINNED SAVED STREAM rather than a mark, so the stream
        #     is the pose: which stream opened and how much of it was read is
        #     the only thing that says where the arm was. The checker's
        #     INPUT_STREAM_REPLAYED reads exactly that, and these arms are the
        #     ones that declare it, so the declaration is driven here against
        #     the runner's own record rather than against a fixture alone.
        $rc = Run-Runner @('-DryRun', '-Out', (Join-Path $Work '_dry-control'),
                           '-Control', 'runway',
                           '-AssetOverrideDir', $assetDir, '-WorldAssetRoot', $waDir,
                           '-TexpackRoot', $tpDir)
        Ck '-Control: the dry run completes and the SECOND-POSE group is built' `
           (($rc.exit -eq 0) -and ($rc.out -match '=== SECOND-POSE ===') -and
            (-not ($rc.out -match 'NOT BUILT: E-pose2-off, E-pose2-on'))) ("exit $($rc.exit)")
        $cblocks = @{}
        $ccur = $null
        foreach ($line in ($rc.out -split "`n")) {
            if ($line -match '^\[([A-Za-z0-9-]+)\] phase ') { $ccur = $Matches[1]; $cblocks[$ccur] = @() }
            elseif ($ccur) { $cblocks[$ccur] += $line }
        }
        foreach ($an in @('E-pose2-off', 'E-pose2-on')) {
            $blk = ($cblocks[$an] -join "`n")
            Ck ("-Control: {0} declares the pinned stream, its reads and its hash" -f $an) `
               (($blk) -and ($blk -match 'control\s+runway \(\d+ reads\), sha256 [0-9a-f]{64}') -and
                ($blk -match 'SL_INPUT=')) $blk
            Ck ("-Control: {0} carries INPUT_STREAM_REPLAYED, so the stream it replayed is witnessed" -f $an) `
               (($blk) -and ($blk -match 'INPUT_STREAM_REPLAYED')) $blk
            Ck ("-Control: {0} declares its capture grid, its unit and its expected moments" -f $an) `
               (($blk) -and
                ($blk -match 'shots\s+gate=\w+ first=\d+ last=\d+ every=\d+ read_first=\d+ read_last=\d+') -and
                ($blk -match 'expected_moments = [\d, ]+\s+count=\d+\s+\d+x\d+')) $blk
        }
        Ck '-Control: with a bound control the second-pose case is no longer declared NOT_COVERED' `
           (-not ($rc.out -match '(?m)^\s+SECOND_POSE\s*$')) $rc.out
        # And a control of ANOTHER stage is refused rather than used: a stream
        # recorded elsewhere boots that stage, which carries no qualified far
        # role here.
        $rb = Run-Runner @('-DryRun', '-Out', (Join-Path $Work '_dry-control-bad'),
                           '-Control', 'dam',
                           '-AssetOverrideDir', $assetDir, '-WorldAssetRoot', $waDir,
                           '-TexpackRoot', $tpDir)
        Ck '-Control: a control that is not pinned here is refused and the arms are not built' `
           (($rb.out -match "-Control 'dam' is") -and
            ($rb.out -match 'NOT BUILT: E-pose2-off, E-pose2-on')) $rb.out

        # -DryRun is a SWITCH and is off by default, so the dry run must name
        # itself and the live mode must warn. Both wordings are asserted: the
        # dry run's from its own output, the live one's from the source, since
        # running the live mode here would launch children.
        Ck '-DryRun: the run names itself a dry run and says nothing is launched' `
           ($r.out -match 'mode\s+DRY RUN \(-DryRun given\) - nothing is launched') $r.out
        Ck 'the LIVE mode says launching is the default and -DryRun is a switch that is off' `
           ([IO.File]::ReadAllText($runner) -match
            'LIVE - GPU children WILL be started\. -DryRun is a switch and is off\.') ''
    }

    $after = @(Get-Process -Name 'sightline' -ErrorAction SilentlyContinue).Count
    Ck 'no sightline.exe was started by this suite' ($after -le $before) `
       ("$before before, $after after")
} finally {
    if ($null -eq $savedRom) { Remove-Item -LiteralPath 'Env:SL_ROM' -ErrorAction SilentlyContinue }
    else { $env:SL_ROM = $savedRom }
}

# ------------------------------------------- 4. the receipt order, from source
# Defect 9 is an ORDERING property of the runner, not a value in a file, so it
# is read off the runner's own source: the intent before the launch, the launch
# receipt before any dependent step, and the exit receipt from a finally block
# so that an exception or a kill still leaves one.
Write-Host ''
Write-Host '--- the per-arm receipt order, read from the runner''s source ---'
$src = [IO.File]::ReadAllText($runner)
$iInvoke = $src.IndexOf('function Invoke-Arm')
$body = $src.Substring($iInvoke)
$iIntent = $body.IndexOf('Write-ArmIntent $arm')
# The needle is the LAUNCH, not the variable that happens to hold the path.
# Pinning the old literal `Start-Process -FilePath $exe` made this ordering
# assertion fail on a rename of that variable - reporting "the intent is not
# written before the child is started" when the ordering was untouched and
# only the name had moved. An ordering gate that breaks on a rename reports
# the wrong defect, and the ordering is what it exists to assert.
$iStart = $body.IndexOf('Start-Process -FilePath ')
$iHandle = $body.IndexOf('$null = $p.Handle')
$iLaunch = $body.IndexOf("'launch.json'")
$iWait = $body.IndexOf('WaitForExit($TimeoutSeconds')
$iFinally = $body.IndexOf('} finally {')
# TWO receipts now live in Invoke-Arm and they are different receipts: one on
# the pre-launch REFUSAL path (a pinned initial save that no longer hashes to
# its pin - the arm is never launched and still leaves a receipt), and the
# EXIT receipt in the finally block. IndexOf alone would read the refusal one
# and report the exit receipt as preceding the finally, so the exit receipt is
# located as the LAST one and the refusal receipt is asserted on its own.
$iReceipt = $body.LastIndexOf('Write-ArmReceipt $arm $rec')
$iRefusedReceipt = $body.IndexOf('Write-ArmReceipt $arm $rec')
$iSavePin = $body.IndexOf('its pinned initial save now hashes')
Ck 'the intent is written before the child is started' `
   (($iIntent -ge 0) -and ($iStart -gt $iIntent)) "intent@$iIntent start@$iStart"
Ck 'the handle is cached before the wait (a nonzero exit is otherwise lost)' `
   (($iHandle -gt $iStart) -and ($iWait -gt $iHandle)) "handle@$iHandle wait@$iWait"
Ck 'the launch receipt (pid and start time) is written before the wait' `
   (($iLaunch -gt $iHandle) -and ($iWait -gt $iLaunch)) "launch@$iLaunch wait@$iWait"
Ck 'the exit receipt is written from the finally block' `
   (($iFinally -ge 0) -and ($iReceipt -gt $iFinally)) "finally@$iFinally receipt@$iReceipt"
Ck 'an arm refused before launch leaves a receipt too, before the try block' `
   (($iSavePin -ge 0) -and ($iRefusedReceipt -gt $iSavePin) -and ($iRefusedReceipt -lt $iFinally)) `
   "savepin@$iSavePin refusedReceipt@$iRefusedReceipt finally@$iFinally"
# Single-quoted: a double-quoted pattern would interpolate $nonzero and
# $status to nothing here and match whatever the source said.
Ck 'the aggregate holds a null or nonzero child exit below PASS' `
   (($src -match '\$null -eq \$_\.exit_code -or \$_\.exit_code -ne 0') -and
    ($src -match '\$nonzero\.Count -gt 0 -and \$status -eq ''PASS''') -and
    ($src -match '\$status = ''FAIL''')) ''
Ck 'a killed arm and a failed launch are counted separately in the aggregate' `
   (($src -match '\$killed = @\(\$script:commands \| Where-Object \{ \$_\.timed_out -eq \$true \}\)') -and
    ($src -match '\$launchFailed = @\(\$script:commands \| Where-Object \{ \$_\.launch_failure \}\)')) ''
Ck 'full qualification is withdrawn by any nonzero command, whatever the witnesses said' `
   ($src -match 'if \(\$nonzero\.Count -gt 0\) \{ \$fullQual = \$false \}') ''
Ck 'the checker''s exit is recorded in the batch record' ($src -match 'checker_exit = \$crc') ''

# ------------------------------------------- 5. the checker's own usage guards
Write-Host ''
Write-Host '--- the checker refuses what it cannot read ---'
$bad = Join-Path $Work '_schema'
New-Item -ItemType Directory -Force -Path $bad | Out-Null
[IO.File]::WriteAllText((Join-Path $bad 'arms.json'), '{"schema":"sightline-perflive/1","arms":[]}',
                        (New-Object System.Text.UTF8Encoding($false)))
$r = Run-Py @($checker, $bad)
Ck 'an arms.json of the old schema is refused rather than half-read' `
   (($r.exit -eq 2) -and ($r.out -match 'schema')) ("exit $($r.exit): $($r.out)")

$bom = Join-Path $Work '_bom'
New-Item -ItemType Directory -Force -Path $bom | Out-Null
$src0 = Join-Path $Work 'pass-subset\arms.json'
if (Test-Path -LiteralPath $src0) {
    # A BOM at the head of a JSON document made it unparseable by a strict
    # reader here before. The checker reads utf-8-sig, so a BOM must not break
    # it - and the RUNNER must never write one.
    $txt = [IO.File]::ReadAllText($src0)
    [IO.File]::WriteAllText((Join-Path $bom 'arms.json'), $txt, (New-Object System.Text.UTF8Encoding($true)))
    Copy-Item -LiteralPath (Join-Path $Work 'pass-subset\B-depth-off') -Destination $bom -Recurse -Force
    Copy-Item -LiteralPath (Join-Path $Work 'pass-subset\B-depth-on') -Destination $bom -Recurse -Force
    $r = Run-Py @($checker, $bom)
    Ck 'the checker reads an arms.json that carries a BOM' ($r.exit -eq 0) ("exit $($r.exit): $($r.out)")
}
Ck 'the runner writes every file through a no-BOM encoder' `
   (($src -match 'UTF8Encoding\(\$false\)') -and (-not ($src -match 'UTF8Encoding\(\$true\)'))) ''
Ck 'the runner writes JSON only through a serializer' `
   ($src -match 'ConvertTo-Json -Depth') ''

Write-Host ''
if ($script:notes.Count -gt 0) {
    Write-Host 'notes:'
    foreach ($nn in $script:notes) { Write-Host "  - $nn" }
    Write-Host ''
}
Write-Host ("perflivetest: {0} ok, {1} failed" -f $script:pass, $script:fail)
Write-Host "  fixture tree: $Work"
Write-Host '  Nothing here is deleted: a failed assertion is only diagnosable against the'
Write-Host '  evidence it was read from.'
if ($script:fail -gt 0) { exit 1 }
exit 0
