<#
.SYNOPSIS
    Install an ordinary glTF model as a Sightline native asset override.

.DESCRIPTION
    Run from an ordinary Windows PowerShell prompt at the repository root:

        .\tools\windows\asset-import.ps1 -Asset boot.nintendo_logo -Input C:\models\mine.glb
        .\tools\windows\asset-import.ps1 -Asset boot.rareware_logo -Input C:\models\other.glb
        .\tools\windows\asset-import.ps1 -Remove boot.nintendo_logo
        .\tools\windows\asset-import.ps1 -Where
        .\tools\windows\asset-import.ps1 -List
        .\tools\windows\asset-import.ps1 -Help

    Export from Blender with File > Export > glTF 2.0 (.glb or .gltf) and point
    -Input at what it wrote. Nothing needs rebuilding: the next launch of
    .\tools\windows\play.ps1 picks the model up, and deleting it (-Remove) puts
    the original logo back.

    NO SOURCE EDIT IS EVER REQUIRED. A model you import this way is YOUR
    content and lives beside the EEPROM, outside the tree.

    SOME MODELS SHIP WITH THE REPOSITORY. data/overrides/ holds committed
    logos, and a fresh clone shows them with no import at all. The search
    order puts YOURS FIRST:

        your install directory  ->  data/overrides  ->  the original logo

    so importing replaces a shipped model, and -Remove brings the shipped one
    back rather than the original. To get the ORIGINAL logos, set
    SL_ASSET_OVERRIDES=0 - that turns the whole feature off, committed models
    included, and the boot screens run their original code path unchanged.

    GAME TEXTURES ARE NEVER EMBEDDED. If a texture in your model IS one of
    the game's own, the importer recognises it by content and stores a
    REFERENCE to it instead of the pixels; the game resolves it from your own
    ROM at load. -GameTextures lists the ones it knows. This is why a
    committed model contains no ROM-derived bytes (project rule 2).

.PARAMETER Asset
    Which logical asset to replace:
        boot.nintendo_logo   the model on the Nintendo boot screen
        boot.rareware_logo   the model on the Rareware boot screen
        boot.goldeneye_logo  the GOLDENEYE logo model on the logo screen
        boot.legal_page      the ARTWORK layer of the Legal screen - six flat
                             quads. The legal WORDING is font-rendered at
                             runtime and is NOT part of the model.

.PARAMETER Input
    A .glb or .gltf file. Its textures must be PNG.

.PARAMETER Remove
    Delete the override you installed for that asset id. If a COMMITTED
    model exists for it in data/overrides, that one renders next; otherwise
    the original does. SL_ASSET_OVERRIDES=0 always gets you the original.

.PARAMETER Where
    Print the resolved override directory and exit. This is the SAME ladder
    the game resolves at runtime:
        $SL_ASSET_OVERRIDE_DIR
        %LOCALAPPDATA%\sightline\assets
        %USERPROFILE%\AppData\Local\sightline\assets
        %TEMP%\sightline\assets

.PARAMETER List
    Show which overrides are currently installed.

.PARAMETER Scale
    A uniform scale baked into the model at import. Default 1.0.

    One glTF unit is one N64 model unit, for every asset - the game holds no
    per-logo scale factor. A model authored in some other unit is fitted here
    instead, once, into the file you install. Every import prints the model's
    bounds and what the target screen frames, and suggests a -Scale when they
    are far apart, so a mis-sized model is a printed number rather than a
    relaunch.

.PARAMETER Output
    Write the converted model to a path of your choosing instead of installing
    it. For inspecting the converted file; the game will not read it there.

.PARAMETER Repo
    Write the model into the REPOSITORY (data/overrides/) instead of your
    install directory, so it ships to everyone who clones.

    This mode REFUSES to write any texture that is not a reference to a game
    texture, UNLESS the glTF itself declares those pixels as the author's own
    with extras.sl_authored plus a non-empty extras.sl_authored_provenance.
    Game-texture detection runs first and turns anything it recognises into a
    reference, so the declaration cannot carry ROM pixels into the tree. See
    docs/project-rules.md, rule 2.

    Models that ship with the repository are normally built from a SOURCE
    package under data/asset-overrides/source/ by build.ps1, not imported by
    hand - see data/asset-overrides/README.md.

.PARAMETER TextureRef
    IMAGE=IDENTIFIER, repeatable. Declares that an image IS a game texture
    when it has been re-saved or edited and so is not recognised by content.
    IMAGE is the image's uri or file name; IDENTIFIER is one of the names
    -GameTextures prints.

.PARAMETER GameTextures
    List the game textures a model may reference, and exit.

.PARAMETER Help
    Print the converter's full help, including the supported glTF subset, the
    coordinate contract and the size limits.
#>
[CmdletBinding()]
param(
    [string]$Asset,
    [string]$Input,
    [string]$Remove,
    [string]$Output,
    [double]$Scale = 1.0,
    [string[]]$TextureRef = @(),
    [switch]$Repo,
    [switch]$GameTextures,
    [switch]$Where,
    [switch]$List,
    [switch]$Help
)

$ErrorActionPreference = 'Stop'

# -Input is bound normally, but $Input inside an advanced function is
# PowerShell's AUTOMATIC pipeline-enumerator variable and shadows the
# parameter - so reading $Input here silently yields an empty enumerator and
# the script prints its usage instead of importing anything. Measured: the
# first run of this file did exactly that. The bound value is read out of
# $PSBoundParameters, which is not shadowed, and the parameter keeps the name
# the owner types.
$InputPath = $PSBoundParameters['Input']

# Repo root from this script's own location - never a literal path.
# NOT named $repo: PowerShell variable names are case-insensitive, so an
# internal $repo and the -Repo switch parameter are THE SAME VARIABLE.
# MEASURED - assigning the repository path to it raised "Cannot convert ... to
# type SwitchParameter" and every invocation failed before doing any work.
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$tool = Join-Path $repoRoot 'tools\asset\gltf_import.py'

if (-not (Test-Path $tool)) {
    Write-Host "sightline: converter missing at $tool"
    exit 1
}

$python = $env:SL_PYTHON
if (-not $python) { $python = Join-Path $repoRoot '.venv\Scripts\python.exe' }
if (-not (Test-Path $python)) {
    $sys = Get-Command python.exe -ErrorAction SilentlyContinue
    if ($sys) { $python = $sys.Source }
}
if (-not (Test-Path $python)) {
    Write-Host 'sightline: no Python found. Set SL_PYTHON, or run tools\windows\setup.ps1.'
    exit 1
}

# The converter imports only the standard library, so no venv packages are
# needed and a bare system Python is fine.
$argv = @($tool)

if ($Help)   { $argv += '--help' }
elseif ($Where)  { $argv += '--where' }
elseif ($List)   { $argv += '--list' }
elseif ($GameTextures) { $argv += '--game-textures' }
elseif ($Remove) { $argv += @('--remove', $Remove) }
else {
    if (-not $Asset -or -not $InputPath) {
        Write-Host ''
        Write-Host 'sightline asset override - install a glTF model'
        Write-Host ''
        Write-Host '  .\tools\windows\asset-import.ps1 -Asset boot.nintendo_logo -Input <file.glb>'
        Write-Host '  .\tools\windows\asset-import.ps1 -Asset boot.rareware_logo -Input <file.glb>'
        Write-Host '  .\tools\windows\asset-import.ps1 -Remove boot.nintendo_logo'
        Write-Host '  .\tools\windows\asset-import.ps1 -Where     # where overrides live'
        Write-Host '  .\tools\windows\asset-import.ps1 -List      # what is installed'
        Write-Host '  .\tools\windows\asset-import.ps1 -GameTextures  # referenceable game textures'
        Write-Host '  .\tools\windows\asset-import.ps1 -Help      # supported glTF subset'
        Write-Host ''
        Write-Host '  set SL_ASSET_OVERRIDES=0 to see the ORIGINAL logos'
        Write-Host '  (turns the whole feature off, committed models included)'
        Write-Host ''
        exit 2
    }
    $argv += @('--asset', $Asset, '--input', $InputPath)
    if ($Output) { $argv += @('--output', $Output) }
    if ($Scale -ne 1.0) { $argv += @('--scale', ([string]$Scale)) }
    if ($Repo) { $argv += '--repo' }
    foreach ($tr in $TextureRef) { $argv += @('--texture-ref', $tr) }
}

& $python $argv
exit $LASTEXITCODE
