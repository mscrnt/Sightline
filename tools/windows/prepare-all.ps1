<#
.SYNOPSIS
    Prepare everything the COMMUNITY HD and XBLA presentations draw, in one
    run: both texture packs, then every XBLA world asset of the campaign.

.DESCRIPTION
    Run from an ordinary Windows PowerShell prompt at the repository root:

        .\tools\windows\prepare-all.ps1

    It is the two preparation scripts, one after the other, with the options
    below handed to whichever takes them:

        .\tools\windows\prepare-textures.ps1          texpacks\community, texpacks\xbla
        .\tools\windows\prepare-xbla-assets.ps1 -All  worldassets\xbla

    The inputs are the ones those scripts read: the Community HD release,
    fetched from its maintainers by prepare-textures.ps1 unless -NoDownload;
    your own XBLA source at texsources\xbla-source.7z (or -XblaArchive); and
    your ROM (baserom.u.z64 or SL_ROM) for the world assets. A set whose
    source is not there is reported by its own script and skipped; the world
    assets need the XBLA source. Nothing is launched, and nothing is
    downloaded except the Community HD release.

    Both steps always run. The exit code is non-zero when either step's is,
    and the closing lines say which.

.PARAMETER XblaArchive
    Your XBLA source, if it is not at texsources\xbla-source.7z. Handed to
    both steps.

.PARAMETER CommunityArchive
    The Community HD release archive, if it is not at
    texsources\community-hd.zip (prepare-textures.ps1).

.PARAMETER NoDownload
    Never touch the network (prepare-textures.ps1).

.PARAMETER TexturesOut
    The texture pack root to write (prepare-textures.ps1 -Out). Defaults to
    the repository's texpacks\.

.PARAMETER WorldOut
    The world-asset root to write (prepare-xbla-assets.ps1 -Out). Defaults
    to the repository's worldassets\.

.PARAMETER Force
    Redo work whose inputs have not changed, in both steps.

.PARAMETER KeepScratch
    Leave each step's extracted working copy under build\ (both steps).
#>
[CmdletBinding()]
param(
    [string]$XblaArchive,
    [string]$CommunityArchive,
    [switch]$NoDownload,
    [string]$TexturesOut,
    [string]$WorldOut,
    [switch]$Force,
    [switch]$KeepScratch
)

$ErrorActionPreference = 'Stop'

$tex = @{}
if ($XblaArchive)      { $tex['XblaArchive'] = $XblaArchive }
if ($CommunityArchive) { $tex['CommunityArchive'] = $CommunityArchive }
if ($NoDownload)       { $tex['NoDownload'] = $true }
if ($TexturesOut)      { $tex['Out'] = $TexturesOut }
if ($Force)            { $tex['Force'] = $true }
if ($KeepScratch)      { $tex['KeepScratch'] = $true }

$wa = @{ All = $true }
if ($XblaArchive) { $wa['XblaArchive'] = $XblaArchive }
if ($WorldOut)    { $wa['Out'] = $WorldOut }
if ($Force)       { $wa['Force'] = $true }
if ($KeepScratch) { $wa['KeepScratch'] = $true }

Write-Host ''
Write-Host '==== prepare-all: 1 of 2 - texture packs (prepare-textures.ps1)'
& (Join-Path $PSScriptRoot 'prepare-textures.ps1') @tex
$texRc = $LASTEXITCODE

Write-Host ''
Write-Host '==== prepare-all: 2 of 2 - XBLA world assets (prepare-xbla-assets.ps1 -All)'
& (Join-Path $PSScriptRoot 'prepare-xbla-assets.ps1') @wa
$waRc = $LASTEXITCODE

Write-Host ''
Write-Host ("prepare-all: texture packs exit {0}; world assets exit {1}." -f $texRc, $waRc)
if ($texRc -ne 0 -or $waRc -ne 0) {
    Write-Host '  See the step above whose exit is not 0.'
    exit 1
}
Write-Host 'Launch with .\tools\windows\play.ps1 -Level dam, then F5 cycles ORIGINAL -> COMMUNITY HD -> XBLA.'
exit 0
