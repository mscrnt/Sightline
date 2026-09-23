<#
.SYNOPSIS
    Prepare the project-local texture packs from your own copies of the
    sources, so a development launch can actually show them.

.DESCRIPTION
    Run from an ordinary Windows PowerShell prompt at the repository root:

        .\tools\windows\prepare-textures.ps1              # every set you have
        .\tools\windows\prepare-textures.ps1 -Community
        .\tools\windows\prepare-textures.ps1 -Xbla

    It works from the repository's private, gitignored source directory:

        texsources\community-hd.zip   the Community HD project's official
                                      release - FETCHED FOR YOU from the
                                      maintainers' own GitHub releases if it
                                      is not there yet
        texsources\xbla-source.7z     your own user-supplied XBLA source

    and writes what the engine reads:

        texpacks\community\<hex4>.sltx
        texpacks\xbla\<hex4>.sltx

    `texpacks\` is the development pack root: tools\windows\play.ps1 points
    the game at it automatically when it exists, so after one run of this
    script the TEXTURES setting has something to select. Both directories
    are gitignored and neither is ever committed, packaged or exported -
    exactly the boundary baserom.u.z64 lives behind.

    COMMUNITY HD IS DOWNLOADED FROM ITS MAINTAINERS, ON THIS MACHINE, and
    never from anything of Sightline's: tools\texpack\get-textures.ps1 asks
    github.com for the release pinned in tools\texpack\community-source.json
    (tag, asset, size, SHA-256), verifies what arrives against that record and
    keeps it in texsources\. Sightline redistributes, mirrors and pre-converts
    nothing - this is the local adapter for the official pack that its
    maintainers asked for. Pass -NoDownload to keep it entirely offline.

    THE XBLA SET IS USER-SUPPLIED AND IS NOT OBTAINED FOR YOU. Sightline
    neither ships, locates nor downloads a source, and nothing here points at
    one: put your own copy at the path above.

    Neither set is assumed complete, and neither needs to be: the mapping
    tables in tools\texpack\mapping name the textures Sightline can key to
    the game's own image table, a texture your copy lacks is simply absent
    from the pack, and the game draws its own artwork there.

    Rerunning is safe: every file is written through a temporary and renamed
    into place, so a second run replaces a pack rather than half-writing one.

.PARAMETER Community
    Prepare the Community HD set only. With neither switch, every set whose
    source is present is prepared.

.PARAMETER Xbla
    Prepare the XBLA set only.

.PARAMETER CommunityArchive
    The official Community HD release archive to read, if it is not at the
    conventional texsources\community-hd.zip. Nothing is downloaded when this
    is given.

.PARAMETER NoDownload
    Never touch the network. The Community HD set is prepared only if a copy
    of the release is already in texsources\ (or named with
    -CommunityArchive); otherwise it is reported and skipped.

.PARAMETER Force
    Redo work whose inputs have not changed - by default an XBLA pack whose
    source and mapping are the ones it was built from is left alone.

.PARAMETER XblaArchive
    The user-supplied XBLA source archive to read, if it is not at the
    conventional texsources\xbla-source.7z.

.PARAMETER Out
    The pack root to write. Defaults to the repository's texpacks\.

.PARAMETER SelfTest
    Run the tooling's own checks and stop. Every fixture is synthetic - a
    generated image, a zip built in a temporary directory, a few bytes that
    are deliberately not a container - so this needs no source, no pack and
    no ROM, and it is the check to run after touching anything under
    tools\texpack.

.PARAMETER KeepScratch
    Leave the extracted working copy under build\texpack\ instead of
    removing it. It is large; it exists only so a rerun is quick.
#>
[CmdletBinding()]
param(
    [switch]$Community,
    [switch]$Xbla,
    [string]$CommunityArchive,
    [string]$XblaArchive,
    [string]$Out,
    [switch]$SelfTest,
    [switch]$KeepScratch,
    [switch]$NoDownload,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

if (-not $Community -and -not $Xbla) { $Community = $true; $Xbla = $true }
if (-not $Out) { $Out = Join-Path $repo 'texpacks' }
$sources = Join-Path $repo 'texsources'
if (-not $CommunityArchive) { $CommunityArchive = Join-Path $sources 'community-hd.zip' }
if (-not $XblaArchive)      { $XblaArchive      = Join-Path $sources 'xbla-source.7z' }

$python = $env:SL_PYTHON
if (-not $python) { $python = Join-Path $repo '.venv\Scripts\python.exe' }
if (-not (Test-Path $python)) {
    $sys = Get-Command python.exe -ErrorAction SilentlyContinue
    if ($null -eq $sys) {
        Write-Host 'prepare-textures: no Python. Run .\tools\windows\setup.ps1.'
        exit 1
    }
    $python = $sys.Source
}

$prep = Join-Path $repo 'tools\texpack\prepare.py'
$ok = $true

if ($SelfTest) {
    & $python (Join-Path $repo 'tools\texpack\selftest.py')
    exit $LASTEXITCODE
}

# ---------------------------------------------------------------------------
# Community HD: obtained from its own maintainers, on this machine, and read
# straight out of the release archive. No extraction step and no external
# tool - the conversion reads the archive as it stands, which is the "consume
# the official pack without modifying it" direction.
# ---------------------------------------------------------------------------
$getTex = Join-Path $repo 'tools\texpack\get-textures.ps1'
$pinFile = Join-Path $repo 'tools\texpack\community-source.json'

if ($Community) {
    $supplied = $PSBoundParameters.ContainsKey('CommunityArchive')
    if (-not (Test-Path -LiteralPath $CommunityArchive) -and -not $supplied -and -not $NoDownload) {
        New-Item -ItemType Directory -Force -Path $sources | Out-Null
        Write-Host ''
        Write-Host 'community: no copy of the pack here yet - asking its maintainers for the'
        Write-Host '           pinned release. Nothing of Sightline''s is involved in this.'
        & $getTex -FetchOnly -Cache $sources | Out-Null
        if ($LASTEXITCODE -ne 0) { $ok = $false }
    }

    if (-not (Test-Path -LiteralPath $CommunityArchive)) {
        Write-Host ''
        Write-Host 'prepare-textures: no Community HD source.'
        Write-Host "  Looked for: $CommunityArchive"
        if ($NoDownload) {
            Write-Host '  -NoDownload was given, so nothing was fetched. Run without it, or put'
            Write-Host '  an official release archive at that path (or pass -CommunityArchive).'
        }
        Write-Host ''
        if ($Xbla) { $ok = $false } else { exit 1 }
    } else {
        # The pin is what makes a rerun cheap and a copy identifiable: the
        # archive already here is verified against it rather than re-fetched.
        $pin = Get-Content -Raw -LiteralPath $pinFile | ConvertFrom-Json
        $have = (Get-FileHash -LiteralPath $CommunityArchive -Algorithm SHA256).Hash.ToLowerInvariant()
        Write-Host ''
        if ($have -eq $pin.pinned.sha256.ToLowerInvariant()) {
            Write-Host "community: $($pin.pinned.asset) ($($pin.pinned.tag)) - size and sha256 match the pin"
        } else {
            Write-Host 'community: this archive is NOT the pinned release (sha256 differs); it is'
            Write-Host '           converted as you supplied it.'
        }
        # Same stamp rule as the XBLA set below: a pack built from this
        # archive and this mapping is already current. The stamp lives in
        # gitignored build output.
        $cMap = Join-Path $repo 'tools\texpack\mapping\community.json'
        $cMapHash = (Get-FileHash -LiteralPath $cMap -Algorithm SHA256).Hash.ToLowerInvariant()
        $cOutDir = Join-Path $Out 'community'
        $cCount = 0
        if (Test-Path -LiteralPath $cOutDir) {
            $cCount = @(Get-ChildItem -LiteralPath $cOutDir -Filter '*.sltx' -File).Count
        }
        $cStampDir = Join-Path $repo 'build\texpack'
        $cStampFile = Join-Path $cStampDir 'community.stamp'
        $cStampNow = "1 $have $cMapHash $cCount"
        $rc = 0
        if (-not $Force -and $cCount -gt 0 -and (Test-Path -LiteralPath $cStampFile) -and
            ((Get-Content -Raw -LiteralPath $cStampFile).Trim() -eq $cStampNow)) {
            Write-Host "community: the pack is current for this archive - nothing to do."
            Write-Host '           Pass -Force to convert it again anyway.'
        } else {
        Write-Host "community: reading $CommunityArchive"
        & $python $prep community --archive $CommunityArchive --out $Out
        $rc = $LASTEXITCODE
        if ($rc -eq 3) {
            # prepare.py exits 3 when a developer Python prerequisite is
            # absent. The converter the release package ships needs none, so
            # fall back to it rather than stopping: they are the same format
            # and produce the same files.
            Write-Host 'community: falling back to the packaged PowerShell converter (no Python'
            Write-Host '           prerequisites needed; byte-for-byte the same output).'
            & $getTex -Archive $CommunityArchive -Out $Out -AnyArchive -Quiet
            $rc = $LASTEXITCODE
        }
        if ($rc -eq 0) {
            $n = @(Get-ChildItem -LiteralPath $cOutDir -Filter '*.sltx' -File).Count
            New-Item -ItemType Directory -Force -Path $cStampDir | Out-Null
            Set-Content -LiteralPath $cStampFile -Encoding ascii -Value ("1 $have $cMapHash $n")
        }
        }
        if ($rc -ne 0) { $ok = $false }
    }
}

# ---------------------------------------------------------------------------
# XBLA: the source is a container the conversion cannot read in place, so a
# working copy of ONLY the bundles the accepted mapping names is extracted
# into build\texpack\ (gitignored build output) and read from there.
# ---------------------------------------------------------------------------
if ($Xbla) {
    if (-not (Test-Path -LiteralPath $XblaArchive)) {
        Write-Host ''
        Write-Host 'prepare-textures: no XBLA source.'
        Write-Host "  Looked for: $XblaArchive"
        Write-Host '  The XBLA set is user-supplied: Sightline neither ships, locates nor'
        Write-Host '  downloads a source. Put your own copy at that path (or pass'
        Write-Host '  -XblaArchive <path>) and run this again.'
        Write-Host ''
        if (-not $Community) { exit 1 }
    } else {
        # A pack whose source and mapping are the ones it was built from is
        # already current: the extraction and the decode are the expensive
        # part of this script, so the source is identified by SHA-256 and the
        # work is skipped. The stamp lives in gitignored build output, so no
        # fingerprint of a user-supplied archive is ever recorded in the tree.
        $mappingFile = Join-Path $repo 'tools\texpack\mapping\xbla-accepted.json'
        $stampDir = Join-Path $repo 'build\texpack'
        $stampFile = Join-Path $stampDir 'xbla.stamp'
        $xblaHash = (Get-FileHash -LiteralPath $XblaArchive -Algorithm SHA256).Hash.ToLowerInvariant()
        $mapHash = (Get-FileHash -LiteralPath $mappingFile -Algorithm SHA256).Hash.ToLowerInvariant()
        $outDir = Join-Path $Out 'xbla'
        $outCount = 0
        if (Test-Path -LiteralPath $outDir) {
            $outCount = @(Get-ChildItem -LiteralPath $outDir -Filter '*.sltx' -File).Count
        }
        $stampNow = "1 $xblaHash $mapHash $outCount"
        $current = $false
        if (-not $Force -and $outCount -gt 0 -and (Test-Path -LiteralPath $stampFile)) {
            $current = ((Get-Content -Raw -LiteralPath $stampFile).Trim() -eq $stampNow)
        }
        if ($current) {
            Write-Host ''
            Write-Host "xbla: source unchanged (sha256 verified) and the pack is current - nothing to do."
            Write-Host '      Pass -Force to convert it again anyway.'
        } else {
        # 7-Zip is a DEVELOPER tool here, like the compiler: discovered at
        # runtime, never a literal path, and not a runtime dependency of the
        # game. SL_SEVENZIP overrides the ladder.
        $sz = $env:SL_SEVENZIP
        if (-not $sz -or -not (Test-Path -LiteralPath $sz)) {
            $sz = $null
            $cmd = Get-Command 7z.exe -ErrorAction SilentlyContinue
            if ($cmd) { $sz = $cmd.Source }
        }
        if (-not $sz) {
            foreach ($base in @($env:ProgramFiles, ${env:ProgramFiles(x86)})) {
                if ($base) {
                    $c = Join-Path $base '7-Zip\7z.exe'
                    if (Test-Path -LiteralPath $c) { $sz = $c; break }
                }
            }
        }
        if (-not $sz) {
            Write-Host ''
            Write-Host 'prepare-textures: no 7-Zip to open the XBLA source with.'
            Write-Host '  Install 7-Zip, or point SL_SEVENZIP at a 7z.exe. It is a developer'
            Write-Host '  tool for this one step; the game never needs it.'
            Write-Host ''
            $ok = $false
        } else {
            $scratch = Join-Path $repo 'build\texpack\xbla'
            New-Item -ItemType Directory -Force -Path $scratch | Out-Null

            # Only the bundles the accepted mapping names are taken out of the
            # source - the mapping is the list, so the working copy never
            # grows beyond what the conversion needs. The source's own
            # top-level layout is DISCOVERED from its index rather than
            # assumed, so nothing here describes any particular source.
            $mapping = Join-Path $repo 'tools\texpack\mapping\xbla-accepted.json'
            $bundles = (Get-Content -Raw -LiteralPath $mapping | ConvertFrom-Json).entries |
                       ForEach-Object { $_.bundle -replace '/', '\' } | Sort-Object -Unique

            Write-Host ''
            Write-Host "xbla: indexing $XblaArchive"
            $index = & $sz l $XblaArchive -slt -ba | Select-String -SimpleMatch 'Path = '
            $paths = $index | ForEach-Object { $_.Line.Substring($_.Line.IndexOf('Path = ') + 7) }
            $wanted = New-Object System.Collections.Generic.List[string]
            foreach ($b in $bundles) {
                $suffix = '\' + $b + '\'
                foreach ($p in $paths) {
                    if ($p.EndsWith('.bin') -or $p.EndsWith('.rba')) {
                        if ($p.Contains($suffix) -and
                            ($p.Substring(0, $p.LastIndexOf('\'))).EndsWith($b)) {
                            $wanted.Add($p)
                        }
                    }
                }
            }
            if ($wanted.Count -eq 0) {
                Write-Host 'prepare-textures: that source holds none of the mapped bundles.'
                Write-Host '  It is refused rather than guessed at. Check that it is the'
                Write-Host '  source the XBLA mapping was made against.'
                $ok = $false
            } else {
                $list = Join-Path (Join-Path $repo 'build\texpack') 'include.txt'
                ($wanted | Sort-Object -Unique) | Set-Content -LiteralPath $list -Encoding ascii
                Write-Host "  $($bundles.Count) bundles, $($wanted.Count) files -> $scratch"
                & $sz x $XblaArchive "-o$scratch" "@$list" -y -bso0 -bsp0 | Out-Null
                if ($LASTEXITCODE -ne 0) {
                    Write-Host "prepare-textures: 7-Zip failed (exit $LASTEXITCODE) on $XblaArchive."
                    Write-Host '  An unreadable or partial source is refused rather than guessed at.'
                    $ok = $false
                } else {
                    & $python $prep xbla --tree $scratch --out $Out
                    if ($LASTEXITCODE -ne 0) { $ok = $false }
                    if (-not $KeepScratch) {
                        Remove-Item -LiteralPath $scratch -Recurse -Force -ErrorAction SilentlyContinue
                    }
                    if ($LASTEXITCODE -eq 0) {
                        $n = @(Get-ChildItem -LiteralPath $outDir -Filter '*.sltx' -File).Count
                        New-Item -ItemType Directory -Force -Path $stampDir | Out-Null
                        Set-Content -LiteralPath $stampFile -Encoding ascii `
                            -Value ("1 $xblaHash $mapHash $n")
                    }
                }
            }
        }
        }
    }
}

Write-Host ''
foreach ($s in @('community', 'xbla')) {
    $d = Join-Path $Out $s
    if (Test-Path -LiteralPath $d) {
        $n = @(Get-ChildItem -LiteralPath $d -Filter '*.sltx' -File).Count
        Write-Host ("  {0,-10} {1,4} textures  {2}" -f $s, $n, $d)
    } else {
        Write-Host ("  {0,-10}    - not prepared" -f $s)
    }
}
Write-Host ''
Write-Host 'Launch with .\tools\windows\play.ps1 - it finds this pack root on its own.'
Write-Host 'Pick a set in OPTIONS > SETTINGS > DISPLAY, or the watch''s SIGHTLINE > GRAPHICS.'
Write-Host ''
if (-not $ok) { exit 1 }
exit 0
