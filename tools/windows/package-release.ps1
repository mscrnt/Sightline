<#
.SYNOPSIS
    Stage, validate and ZIP one Windows release package from a finished build.

.DESCRIPTION
    Run from an ordinary Windows PowerShell prompt:

        .\tools\windows\package-release.ps1 -Version X.Y.Z -SourceDir <clean checkout> `
            -OutDir <dir> [-Rom <your ROM>] [-CanonicalSha ...] [-CanonicalTag vX.Y.Z] `
            [-PublicSha ...] [-PublicTag vX.Y.Z] [-QuitHint <text>] [-InputHash label=path ...]

    THIS SCRIPT BUILDS NOTHING AND TOUCHES NO REMOTE. It takes the build that
    already sits in <SourceDir>\build\win32 (a normal build.ps1 output, never
    -Demo), stages exactly what a player needs, writes VERSION.txt with the
    provenance it is told and can measure, runs validate-package.ps1 on the
    staged directory, and only then compresses. The finished ZIP is validated
    again, and the .sha256 file is written last. A validator rejection at
    either point leaves no ZIP behind.

    WHAT SHIPS (read off the build and the source tree, never a hand-kept list)
      sightline.exe            <SourceDir>\build\win32\sightline.exe
      the non-system DLLs      the executable's PE import table, walked
                               transitively with objdump (SDL2.dll,
                               libwinpthread-1.dll today)
      data\asset-overrides\    every built *.slmodel under
                               <SourceDir>\build\win32\data\asset-overrides
      Sightline.cmd            tools\windows\release\Sightline.cmd (this tree)
      Get-Textures.cmd         tools\windows\release\Get-Textures.cmd (this tree)
      tools\get-textures.ps1   the optional Community HD fetcher / converter
      tools\community-source.json   which upstream release it asks for
      tools\mapping\community.json  id -> checksum identities (no pixels)
      README.txt               tools\windows\release\README.txt, placeholders filled
      VERSION.txt              generated: version, commits, tags, build date,
                               toolchain identity, .build_key, the SHA-256 of
                               every non-Git build input named with -InputHash,
                               and the SHA-256 of every file in the package
      LICENSES\                <SourceDir>\LICENSES\* plus
                               data\asset-overrides\LICENSE.md, and for every
                               shipped controller model its CC-BY ATTRIBUTION.md
                               under LICENSES\third-party\controllers\<name>\

    The source of the LICENSES is -SourceDir, the checkout the executable was
    built from, so a package never carries a newer tree's notices for an older
    binary. The launcher and README templates come from THIS tree, which is
    release tooling and may be newer than the source; VERSION.txt names both.

.PARAMETER Version
    X.Y.Z - no leading v, no build metadata. Names the folder, the ZIP, the
    checksum file and the VERSION.txt line.

.PARAMETER SourceDir
    The checkout the build in build\win32 was produced from. Its HEAD and
    cleanliness are recorded; a dirty tree is refused.

.PARAMETER Rom
    Passed to the validator for the ROM-derived excerpt scan. Strongly
    recommended: without it a ROM-derived table compiled into the executable
    cannot be detected.

.PARAMETER QuitHint
    The README's "Quitting" line for THIS version (how the game is closed).
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [Parameter(Mandatory = $true)][string]$SourceDir,
    [Parameter(Mandatory = $true)][string]$OutDir,
    [string]$Rom,
    [string]$CanonicalSha,
    [string]$CanonicalTag,
    [string]$PublicSha,
    [string]$PublicTag,
    [string]$QuitHint = 'Close the window (the X, or Alt+F4).',
    [string[]]$InputHash = @(),
    [string]$Msys2Root
)

$ErrorActionPreference = 'Stop'
$tooling = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

function Get-Sha256 { param([string]$P) return (Get-FileHash -LiteralPath $P -Algorithm SHA256).Hash.ToLowerInvariant() }
function Write-TextFileLf {
    param([string]$P, [string]$Text)
    [System.IO.File]::WriteAllText($P, ($Text -replace "`r`n", "`n"), (New-Object System.Text.UTF8Encoding($false)))
}
function Find-Mingw32Bin {
    param([string]$Override)
    $explicit = $Override
    if (-not $explicit) { $explicit = $env:SL_MSYS2_ROOT }
    $roots = @()
    if ($explicit) { $roots = @($explicit) }
    else { $roots = @("$env:SystemDrive\msys64", 'C:\msys64', 'C:\msys32', "$env:ProgramFiles\msys64", "$env:LOCALAPPDATA\Programs\msys64") }
    foreach ($r in $roots) { if ($r -and (Test-Path (Join-Path $r 'mingw32\bin\objdump.exe'))) { return (Join-Path $r 'mingw32\bin') } }
    return $null
}

if ($Version -notmatch '^\d+\.\d+\.\d+$') { Write-Host "package-release: -Version must be X.Y.Z"; exit 1 }
$SourceDir = (Resolve-Path -LiteralPath $SourceDir).Path
$buildDir  = Join-Path $SourceDir 'build\win32'
$exe       = Join-Path $buildDir 'sightline.exe'
if (-not (Test-Path -LiteralPath $exe)) { Write-Host "package-release: no $exe (run build.ps1 in the source checkout first)"; exit 1 }

# the source must be a clean checkout at a known commit
$head  = (& git -C $SourceDir rev-parse HEAD).Trim()
$dirty = @(& git -C $SourceDir status --porcelain)
if ($dirty.Count -gt 0) { Write-Host "package-release: $SourceDir is not clean ($($dirty.Count) entries); refusing."; exit 1 }
if ($CanonicalSha -and $CanonicalSha -ne $head) { Write-Host "package-release: -CanonicalSha $CanonicalSha but the source HEAD is $head; refusing."; exit 1 }
if (-not $CanonicalSha) { $CanonicalSha = $head }

# a normal build only
$keyFile = Join-Path $buildDir '.build_key'
$key = ''
if (Test-Path -LiteralPath $keyFile) { $key = [System.IO.File]::ReadAllText($keyFile).Trim() }
if ($key.Contains('-DSL_DEMO_BUILD')) { Write-Host 'package-release: build\win32 was built with -Demo; a release is never a demo build.'; exit 1 }

$mingwBin = Find-Mingw32Bin -Override $Msys2Root
if ($null -eq $mingwBin) { Write-Host 'package-release: no objdump.exe (MSYS2 mingw32) to read the import table with; refusing to guess the DLL list.'; exit 1 }
$objdump = Join-Path $mingwBin 'objdump.exe'

# ------------------------------------------------------------------ stage --

$name  = "Sightline-v$Version-win32"
$OutDir = (Resolve-Path -LiteralPath (New-Item -ItemType Directory -Force -Path $OutDir)).Path
$stage = Join-Path $OutDir "stage\$name"
if (Test-Path -LiteralPath (Join-Path $OutDir 'stage')) { Remove-Item -Recurse -Force -LiteralPath (Join-Path $OutDir 'stage') }
New-Item -ItemType Directory -Force -Path $stage | Out-Null

Copy-Item -LiteralPath $exe -Destination (Join-Path $stage 'sightline.exe')

# DLLs: transitive PE import walk; non-system = a file of that name in build\win32 or mingw32\bin
$queue = New-Object System.Collections.Generic.Queue[string]
$seen  = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
$dlls  = @()
foreach ($n in (& $objdump -p $exe | Select-String 'DLL Name:\s*(\S+)' | ForEach-Object { $_.Matches[0].Groups[1].Value })) { $queue.Enqueue($n) }
while ($queue.Count -gt 0) {
    $n = $queue.Dequeue()
    if ($seen.Contains($n)) { continue }
    [void]$seen.Add($n)
    $src = $null
    foreach ($cand in @((Join-Path $buildDir $n), (Join-Path $mingwBin $n))) { if (Test-Path -LiteralPath $cand) { $src = $cand; break } }
    if ($null -eq $src) { continue }
    $dlls += $src
    Copy-Item -LiteralPath $src -Destination (Join-Path $stage $n)
    foreach ($n2 in (& $objdump -p $src | Select-String 'DLL Name:\s*(\S+)' | ForEach-Object { $_.Matches[0].Groups[1].Value })) { $queue.Enqueue($n2) }
}

# built asset overrides: the .slmodel files only
$aov = Join-Path $buildDir 'data\asset-overrides'
$ctrl = @()
if (Test-Path -LiteralPath $aov) {
    foreach ($m in (Get-ChildItem -LiteralPath $aov -Recurse -File -Filter *.slmodel)) {
        $r = $m.FullName.Substring((Resolve-Path -LiteralPath $aov).Path.Length).TrimStart('\')
        $dst = Join-Path (Join-Path $stage 'data\asset-overrides') $r
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dst) | Out-Null
        Copy-Item -LiteralPath $m.FullName -Destination $dst
        if ($r -like 'controllers\*') { $ctrl += [System.IO.Path]::GetFileNameWithoutExtension($m.Name) }
    }
}

# launcher and README from THIS tree
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'release\Sightline.cmd') -Destination (Join-Path $stage 'Sightline.cmd')

# The OPTIONAL texture-pack fetcher (#47). Four files, all of them identities
# and code: the entry point beside the launcher, the PowerShell that fetches
# and converts, the record of WHICH upstream release is wanted (repository,
# tag, asset, size, SHA-256) and the id -> checksum mapping. NO PACK BYTE IS
# STAGED and none exists to stage: what the player runs downloads from the
# pack's own maintainers, onto their own machine. Named files, as everything
# else here is - never a directory of the checkout.
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'release\Get-Textures.cmd') -Destination (Join-Path $stage 'Get-Textures.cmd')
New-Item -ItemType Directory -Force -Path (Join-Path $stage 'tools\mapping') | Out-Null
Copy-Item -LiteralPath (Join-Path $SourceDir 'tools\texpack\get-textures.ps1') -Destination (Join-Path $stage 'tools\get-textures.ps1')
Copy-Item -LiteralPath (Join-Path $SourceDir 'tools\texpack\community-source.json') -Destination (Join-Path $stage 'tools\community-source.json')
Copy-Item -LiteralPath (Join-Path $SourceDir 'tools\texpack\mapping\community.json') -Destination (Join-Path $stage 'tools\mapping\community.json')
$readme = [System.IO.File]::ReadAllText((Join-Path $PSScriptRoot 'release\README.txt'))
$readme = $readme.Replace('{{VERSION}}', $Version).Replace('{{QUIT}}', $QuitHint)
Write-TextFileLf (Join-Path $stage 'README.txt') $readme

# licences from the SOURCE checkout
$lic = Join-Path $stage 'LICENSES'
New-Item -ItemType Directory -Force -Path $lic | Out-Null
foreach ($f in (Get-ChildItem -LiteralPath (Join-Path $SourceDir 'LICENSES') -File)) { Copy-Item -LiteralPath $f.FullName -Destination (Join-Path $lic $f.Name) }
Copy-Item -LiteralPath (Join-Path $SourceDir 'data\asset-overrides\LICENSE.md') -Destination (Join-Path $lic 'asset-overrides-LICENSE.md')
foreach ($c in $ctrl) {
    $attr = Join-Path $SourceDir "data\asset-overrides\source\controllers\$c\ATTRIBUTION.md"
    if (-not (Test-Path -LiteralPath $attr)) { Write-Host "package-release: controller model $c ships but $attr is missing; refusing."; exit 1 }
    $d = Join-Path $lic "third-party\controllers\$c"
    New-Item -ItemType Directory -Force -Path $d | Out-Null
    Copy-Item -LiteralPath $attr -Destination (Join-Path $d 'ATTRIBUTION.md')
}

# ------------------------------------------------------------ VERSION.txt --

$gcc = Join-Path $mingwBin 'i686-w64-mingw32-gcc.exe'
$gccVer = ''; if (Test-Path $gcc) { $gccVer = (& $gcc --version | Select-Object -First 1) }
$windresVer = ''; if (Test-Path (Join-Path $mingwBin 'windres.exe')) { $windresVer = (& (Join-Path $mingwBin 'windres.exe') --version | Select-Object -First 1) }
$sdlVer = ''; $sdl = Join-Path $stage 'SDL2.dll'; if (Test-Path $sdl) { $sdlVer = (Get-Item $sdl).VersionInfo.FileVersion }
$toolingHead = (& git -C $tooling rev-parse HEAD 2>$null)
$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("Sightline release provenance")
$lines.Add("version            $Version")
$lines.Add("canonical-commit   $CanonicalSha")
if ($CanonicalTag) { $lines.Add("canonical-tag      $CanonicalTag") }
if ($PublicSha)    { $lines.Add("public-commit      $PublicSha  (the sanitized export of the canonical commit; the only commit visible on github.com/mscrnt/Sightline)") }
if ($PublicTag)    { $lines.Add("public-tag         $PublicTag") }
$lines.Add("build-date-utc     " + (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ'))
$lines.Add("build-type         normal (tools/windows/build.ps1, no -Demo)")
$lines.Add("toolchain-gcc      $gccVer")
$lines.Add("toolchain-windres  $windresVer")
$lines.Add("toolchain-sdl2     $sdlVer (SDL2.dll file version)")
$lines.Add("build-key          $key")
if ($toolingHead) { $lines.Add("packaging-tooling  $($toolingHead.Trim()) (tools/windows/package-release.ps1, validate-package.ps1, release/)") }
foreach ($ih in $InputHash) {
    $eq = $ih.IndexOf('=')
    if ($eq -lt 1) { Write-Host "package-release: -InputHash entries are label=path (got '$ih')"; exit 1 }
    $lbl = $ih.Substring(0, $eq); $p = $ih.Substring($eq + 1)
    if (-not (Test-Path -LiteralPath $p)) { Write-Host "package-release: -InputHash $lbl names a missing file $p"; exit 1 }
    $lines.Add(("build-input        {0}  {1}  {2} bytes" -f $lbl, (Get-Sha256 $p), (Get-Item -LiteralPath $p).Length))
}
$lines.Add("")
$lines.Add("files (sha256  size  path)")
$stagePrefix = (Resolve-Path -LiteralPath $stage).Path.TrimEnd('\') + '\'
foreach ($f in (Get-ChildItem -LiteralPath $stage -Recurse -File | Sort-Object FullName)) {
    $r = $f.FullName.Substring($stagePrefix.Length) -replace '\\', '/'
    $lines.Add(("{0}  {1,10}  {2}" -f (Get-Sha256 $f.FullName), $f.Length, $r))
}
$lines.Add(("{0}  {1,10}  {2}" -f '(this file)', '', 'VERSION.txt'))
Write-TextFileLf (Join-Path $stage 'VERSION.txt') (($lines -join "`n") + "`n")

Write-Host ''
Write-Host "package-release: staged $name"
foreach ($f in (Get-ChildItem -LiteralPath $stage -Recurse -File | Sort-Object FullName)) {
    Write-Host ("  {0,10}  {1}" -f $f.Length, ($f.FullName.Substring($stagePrefix.Length) -replace '\\', '/'))
}

# ---------------------------------------------------------------- validate --

$validator = Join-Path $PSScriptRoot 'validate-package.ps1'
$vArgs = @{ Path = $stage; Version = $Version; Objdump = $objdump }
if ($Rom) { $vArgs['Rom'] = $Rom }
Write-Host ''
Write-Host 'package-release: validating the staged directory'
& $validator @vArgs
if ($LASTEXITCODE -ne 0) {
    Write-Host 'package-release: the staged package was REJECTED; no ZIP written.'
    exit 1
}

# --------------------------------------------------------------- compress --

$zip = Join-Path $OutDir "$name.zip"
Remove-Item -Force -LiteralPath $zip -ErrorAction SilentlyContinue
Remove-Item -Force -LiteralPath "$zip.sha256" -ErrorAction SilentlyContinue
Compress-Archive -LiteralPath $stage -DestinationPath $zip -CompressionLevel Optimal

Write-Host ''
Write-Host 'package-release: validating the finished ZIP'
$vArgs['Path'] = $zip
& $validator @vArgs
if ($LASTEXITCODE -ne 0) {
    Remove-Item -Force -LiteralPath $zip -ErrorAction SilentlyContinue
    Write-Host 'package-release: the ZIP was REJECTED and has been removed.'
    exit 1
}

$hash = Get-Sha256 $zip
Write-TextFileLf "$zip.sha256" ("{0}  {1}`n" -f $hash, "$name.zip")
Write-Host ''
Write-Host "package-release: done"
Write-Host ("  {0}  {1} bytes" -f $zip, (Get-Item -LiteralPath $zip).Length)
Write-Host ("  sha256 {0}" -f $hash)
Write-Host ("  {0}.sha256" -f $zip)
exit 0
