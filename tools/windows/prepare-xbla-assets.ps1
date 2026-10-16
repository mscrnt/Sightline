<#
.SYNOPSIS
    Prepare the project-local WORLD ASSETS from your own copy of the source,
    so a development launch can actually draw them (#68).

.DESCRIPTION
    Run from an ordinary Windows PowerShell prompt at the repository root:

        .\tools\windows\prepare-xbla-assets.ps1 -DamSkydome

    It works from the repository's private, gitignored source directory - the
    same one the texture packs read, and the same boundary baserom.u.z64 lives
    behind:

        texsources\xbla-source.7z     your own user-supplied source

    and writes what the engine reads:

        worldassets\xbla\skydome\dam.slwa

    `worldassets\` is the development world-asset root: tools\windows\play.ps1
    points the game at it automatically when it exists, so after one run of
    this script `-WorldAssets XBLA` has something to draw. Both directories are
    gitignored and neither is ever committed, packaged or exported.

    THIS IS SEPARATE FROM prepare-textures.ps1 AND DOES NOT TOUCH IT. Textures
    are a per-texture substitution keyed by the game's own texture number; a
    world asset is a whole additive item keyed by class and item path. They
    share a source archive and nothing else - no mapping table, no pack, no
    setting.

    THE SOURCE IS USER-SUPPLIED AND IS NOT OBTAINED FOR YOU. Sightline neither
    ships, locates nor downloads one, and nothing here points at one: put your
    own copy at the path above.

    Rerunning is cheap and safe: the source and the item are identified by
    SHA-256 in gitignored build output, a bundle already built from them is
    left alone, and every file is written through a temporary and renamed into
    place, so a bundle is only ever complete or unchanged.

.PARAMETER DamSkydome
    Prepare the Dam skydome - the ADDITIVE asset: remaster-only geometry with
    no counterpart in the game's own picture, drawn as well as it.

    The bundle also carries the GENERATED ZENITH CLOUDS (docs/divergences.md
    D-023): a cap of cloud synthesised here from the dome image's own cloud
    band, as one extra flagged draw. It is a deliberate divergence from the
    remaster, whose dome is clear overhead; SL_SKY_ZENITH=0 at runtime draws
    the source dome exactly. Nothing in the source is altered. The same as
    -Level dam -Skydome.

.PARAMETER Skydome
    Prepare the skydome of the stage -Level names, the way -DamSkydome
    prepares the Dam's (refused when the source holds none):
        -Level runway -Skydome
    The generated zenith clouds (D-023) are written for the domes that
    decision names - the Dam's, Runway's, Surface's, Surface 2's and Frigate's;
    any other stage's dome is the
    source's as authored. It is drawn beneath that stage's background.

.PARAMETER DamBackground
    Prepare the Dam's whole static background - the REPLACEMENT asset: it
    stands in for the stage's own static background geometry, so under
    PRESENTATION XBLA the original emission is suppressed and this is drawn in
    its place. The same as -Level dam -Background.

    It also derives the remaster's STAGE FOG (xbla\environment.slen): the
    per-environment fog colour and range the replacement's own programs fog
    toward, read locally from the executable in the same source. Only those
    values are written; the executable itself is never kept. A source this
    cannot read leaves the replacement unfogged and says so.

.PARAMETER Background
    Prepare the whole static background of the stage -Level names, the way
    -DamBackground prepares the Dam's: the source's background item of that
    name (refused when the source holds none), and the remaster's stage fog
    beside it:
        -Level facility -Background
    The runtime draws a stage's background only for the stages it names
    (src/native/sl_world_assets.c): the Dam, Facility, Runway, Surface,
    Bunker 1 - whose item the source names `bunker`, so it is prepared as
    -Level bunker -Background - Silo, Frigate, Bunker 2 and Statue, whose
    item the source names `statuepark` (-Level statuepark -Background; the
    source holds no dome for it), Archives (-Level archives -Background;
    an interior, no dome either), Streets (-Level streets -Background;
    no dome in the source), Depot (-Level depot -Background; no dome
    in the source either), Train (-Level train -Background; no dome
    either), Jungle (-Level jungle -Background; no dome either),
    Control (-Level control -Background; an interior, no dome) and
    Caverns, whose item the source names `cavern` (-Level cavern
    -Background; the stage's other items are -Level caverns).
    Surface 2 draws Surface's
    background (the source has no `background/surface2`; -Level surface
    -Background prepares it) under a dome of its own, -Level surface2 -Skydome.
    On every one but the Dam
    SL_WA_PREVIEW=0 keeps the game's rooms with the texture set's artwork instead
    (docs/world-assets.md, "Facility's background").

    Silo's background is prepared with the game's teal on the rail-end caps
    the remaster whitened (docs\divergences.md D-033), read from your ROM.
    SL_WA_AS_AUTHORED=D-033 prepares it as the source authors it.

.PARAMETER Characters
    Prepare CHARACTER and HEAD items (#70): the source's REMASTERED item for
    a game model, converted into a skinned bundle the game's own skeleton
    deforms at runtime. Under PRESENTATION XBLA it is drawn in place of that
    model; a model with no prepared item draws as the game's.

    With -Level, the items are the stage's whole character set, DERIVED from
    the game's own tables - every body its setup's guards, its AI spawns and
    their clones can wear, and every head those bodies can be given,
    including the whole random-head pool a guard with no fixed head draws
    from (tools\worldasset\stagechars.py). Every item the source carries is
    prepared in one pass; every one it does not is reported as SOURCE
    ABSENT, and that part keeps the game's own model:
        -Level dam -Characters

    Followed by a list instead, it prepares exactly the named items - the
    diagnostic form, named by class and item as the game names the model:
    char/<item> for the body file C<item>Z, head/<item> for the head file
    C<item>Z:
        -Characters char/greatguard2,head/headlee

.PARAMETER ViewModels
    Prepare every FIRST-PERSON model the player can see on the stage (#70):
    each weapon's view model - the gun and the hand holding it, one item per
    weapon, the same item in both hands of a pair - and the watch hand.
    The watch hand's sleeves are paired with the game's costume cuffs from
    the game's own watch-hand model, read from your ROM (baserom.u.z64 at
    the repository root, or SL_ROM); without it the watch hand is refused
    and the game's is drawn.

    The set is DERIVED from the game's own tables
    (tools\worldasset\viewmodels.py): the stage's intro list gives the
    loadout (and every life begins with the fist), the game's ALL GUNS rule
    (bondinv.c) gives every weapon the cheat puts in reach, the weapon stats
    say which of them the game draws in first person at all (a grenade or a
    mine is not), the gun record table gives each one's model file, and the
    remaster's own executable, read from the same source, names the remaster
    item that stands for each. Every model gets a line: PREPARED, CURRENT,
    SOURCE ABSENT or CONVERSION REFUSED with its reason. A model the source
    has no item for, or one its item is refused for, keeps the game's own:
        -Level dam -ViewModels

    The player's external body (the intro, the ending, the death camera) is
    NOT part of this set: each costume is its own body item, and only
    -Player prepares it. Prepare a stage with -Player to cover both.

.PARAMETER Props
    Prepare the stage's RIGID PROPS (#70): every model its setup's standard
    props and single monitors are drawn from (a mainframe, a desk, a monitor),
    DERIVED from the game's own tables (tools\worldasset\stageprops.py): the
    setup's object list gives the models, the game's prop file table their
    files, and the remaster's own executable, read from the same source, the
    remaster item that stands for each. Both of the source's generations of
    each item are read: the remastered one is drawn, the original one is the
    box that places it on the game's model. Every model gets a line, as
    -ViewModels reports its own:
        -Level dam -Props

    Silo's steel door is prepared with the game's yellow on its hazard bars
    (docs\divergences.md D-032), read from your ROM. To prepare it as the
    source authors it (black and white), set SL_WA_AS_AUTHORED=D-032 and
    prepare again; clearing the variable and preparing again restores it.

.PARAMETER Player
    Everything -ViewModels prepares, and the player's external body, the one
    the game draws in the intro swirl and the death camera (its costume from
    the stage's intro list):
        -Level dam -Player

    Followed by a list instead, it prepares exactly the named items, by the
    GAME'S model names - gun/<item> for the view-model file G<item>Z,
    char/<item> and head/<item> as -Characters takes them:
        -Player gun/wppksil,char/boilerbond

.PARAMETER All
    Prepare EVERYTHING the XBLA presentation draws on the single-player
    campaign, in one run:

        .\tools\windows\prepare-xbla-assets.ps1 -All

    The plan is read from the runtime's own tables (tools\worldasset\
    campaign.py): every stage sl_wa_stage_item_path names - the 20 campaign
    missions - with its background item, and the dome each stage draws
    (borrowed, lent or its own, as the renderer resolves it) where the source
    holds one. The run prepares, in this order and one at a time: each unique
    background (19) and the remaster's stage fog, each unique dome (7), then
    stage by stage the props, characters and player items (-Props -Characters
    -Player). Stage and item names that differ (Bunker 1's `bunker`, Statue's
    `statuepark`, Caverns' `cavern`, Egyptian's `temple` and `egyptian`,
    Surface 2 drawing Surface's background) come from the same tables. The
    archive is hashed and indexed once for the whole run.

    Every item gets its usual line, and a closing summary names any stage
    whose step did not finish cleanly; a refused or absent item keeps the
    game's own model, and the run exits non-zero when any step did.
    -XblaArchive, -Out, -Force and -KeepScratch apply to the whole run. -All
    takes no -Level and no other selection. Texture packs are separate:
    .\tools\windows\prepare-textures.ps1 (or prepare-all.ps1 for both).

.PARAMETER Level
    The stage whose characters -Characters prepares, or whose player items
    -ViewModels / -Player prepare, by the name the launcher takes (dam,
    facility, ...).

.PARAMETER XblaArchive
    The user-supplied source archive to read, if it is not at the conventional
    texsources\xbla-source.7z.

.PARAMETER Out
    The world-asset root to write. Defaults to the repository's worldassets\.
    Each root keeps its own record of what is current (build\worldasset for
    the default, build\worldasset\roots\<id> for any other), so preparing into
    one root never vouches for another's files.

.PARAMETER Force
    Redo work whose inputs have not changed.

.PARAMETER SelfTest
    Run the tooling's own checks and stop. Every fixture is synthetic, so this
    needs no source, no asset and no ROM, and it is the check to run after
    touching anything under tools\worldasset.

.PARAMETER KeepScratch
    Leave the extracted working copy under build\worldasset\ instead of
    removing it.
#>
[CmdletBinding(PositionalBinding = $false)]
param(
    [switch]$DamSkydome,
    [switch]$Skydome,
    [switch]$DamBackground,
    [switch]$Background,
    [switch]$Characters,
    [switch]$ViewModels,
    [switch]$Props,
    [switch]$Player,
    [switch]$All,
    [string]$Level,
    [string]$XblaArchive,
    [string]$Out,
    [switch]$Force,
    [switch]$SelfTest,
    [switch]$KeepScratch,
    # The list after -Characters in its diagnostic form.
    [Parameter(ValueFromRemainingArguments = $true)]
    [object[]]$CharacterItems
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

$python = $env:SL_PYTHON
if (-not $python) { $python = Join-Path $repo '.venv\Scripts\python.exe' }
if (-not (Test-Path $python)) {
    $sys = Get-Command python.exe -ErrorAction SilentlyContinue
    if ($null -eq $sys) {
        Write-Host 'prepare-xbla-assets: no Python. Run .\tools\windows\setup.ps1.'
        exit 1
    }
    $python = $sys.Source
}

if ($SelfTest) {
    & $python (Join-Path $repo 'tools\worldasset\selftest.py')
    exit $LASTEXITCODE
}

$items = @($CharacterItems | ForEach-Object { $_ } | ForEach-Object { "$_" -split ',' } |
           ForEach-Object { $_.Trim() } | Where-Object { $_ })
if ($All) {
    $with = @()
    foreach ($n in 'DamSkydome', 'Skydome', 'DamBackground', 'Background', 'Characters', 'ViewModels',
                   'Props', 'Player') {
        if ((Get-Variable -Name $n -ValueOnly)) { $with += "-$n" }
    }
    if ($Level) { $with += "-Level $Level" }
    if ($items.Count -gt 0) { $with += ($items -join ',') }
    if ($with.Count -gt 0) {
        Write-Host "prepare-xbla-assets: -All prepares the whole campaign; it takes no $($with -join ' ')."
        Write-Host '  .\tools\windows\prepare-xbla-assets.ps1 -All'
        exit 1
    }
}
if ($items.Count -gt 0 -and $Characters -and $Player) {
    Write-Host 'prepare-xbla-assets: a list belongs to -Characters OR -Player, not both.'
    exit 1
}
if ($items.Count -gt 0 -and -not $Characters -and -not $Player) {
    Write-Host "prepare-xbla-assets: '$($items -join ' ')' is not something this prepares."
    exit 1
}

if ($DamBackground) {
    if ($Background -or ($Level -and $Level -ne 'dam')) {
        Write-Host 'prepare-xbla-assets: -DamBackground is -Level dam -Background; name one or the other.'
        exit 1
    }
}
if ($DamSkydome) {
    if ($Skydome) {
        Write-Host 'prepare-xbla-assets: -DamSkydome is -Level dam -Skydome; name one or the other.'
        exit 1
    }
}
if ($Skydome -and -not $Level) {
    Write-Host 'prepare-xbla-assets: -Skydome takes a stage:'
    Write-Host '  .\tools\windows\prepare-xbla-assets.ps1 -Level runway -Skydome'
    exit 1
}
if ($Skydome -and $Level -notmatch '^[a-z0-9_]{1,30}$') {
    Write-Host "prepare-xbla-assets: '$Level' is not a stage name."
    exit 1
}
if ($Background -and -not $Level) {
    Write-Host 'prepare-xbla-assets: -Background takes a stage:'
    Write-Host '  .\tools\windows\prepare-xbla-assets.ps1 -Level facility -Background'
    exit 1
}
if ($Background -and $Level -notmatch '^[a-z0-9_]{1,30}$') {
    Write-Host "prepare-xbla-assets: '$Level' is not a stage name."
    exit 1
}
if (-not $DamSkydome -and -not $Skydome -and -not $DamBackground -and -not $Background -and -not $Characters -and -not $Player -and
    -not $ViewModels -and -not $Props -and -not $All) {
    Write-Host ''
    Write-Host 'prepare-xbla-assets: name what to prepare.'
    Write-Host '  .\tools\windows\prepare-xbla-assets.ps1 -All'
    Write-Host '  .\tools\windows\prepare-xbla-assets.ps1 -DamSkydome'
    Write-Host '  .\tools\windows\prepare-xbla-assets.ps1 -DamBackground'
    Write-Host '  .\tools\windows\prepare-xbla-assets.ps1 -Level facility -Background'
    Write-Host '  .\tools\windows\prepare-xbla-assets.ps1 -Level runway -Skydome'
    Write-Host '  .\tools\windows\prepare-xbla-assets.ps1 -Level dam -Characters'
    Write-Host '  .\tools\windows\prepare-xbla-assets.ps1 -Characters char/greatguard2,head/headlee'
    Write-Host '  .\tools\windows\prepare-xbla-assets.ps1 -Level dam -ViewModels'
    Write-Host '  .\tools\windows\prepare-xbla-assets.ps1 -Level dam -Props'
    Write-Host '  .\tools\windows\prepare-xbla-assets.ps1 -Level dam -Player'
    Write-Host '  .\tools\windows\prepare-xbla-assets.ps1 -Player gun/wppksil,char/boilerbond'
    Write-Host '  .\tools\windows\prepare-xbla-assets.ps1 -SelfTest'
    Write-Host ''
    exit 1
}

# -ViewModels is the player's first-person set alone; -Player adds the body.
# Both run the same derivation below.
$withBody = [bool]$Player
if ($ViewModels) {
    if (-not $Level -or $items.Count -gt 0) {
        Write-Host 'prepare-xbla-assets: -ViewModels takes a stage:'
        Write-Host '  .\tools\windows\prepare-xbla-assets.ps1 -Level dam -ViewModels'
        exit 1
    }
    $Player = [switch]$true
}

$stagechars = Join-Path $repo 'tools\worldasset\stagechars.py'
$viewmodelsTool = Join-Path $repo 'tools\worldasset\viewmodels.py'
$charItems = @(); $playerItems = @()
if ($Characters) { $charItems = $items } elseif ($Player) { $playerItems = $items }
if ($Player) {
    if ($Level -and $playerItems.Count -gt 0) {
        Write-Host 'prepare-xbla-assets: -Player takes a level OR a list of items, not both.'
        exit 1
    }
    if (-not $Level -and $playerItems.Count -eq 0) {
        Write-Host 'prepare-xbla-assets: -Player needs a stage or a list:'
        Write-Host '  .\tools\windows\prepare-xbla-assets.ps1 -Level dam -Player'
        Write-Host '  .\tools\windows\prepare-xbla-assets.ps1 -Player gun/wppksil,char/boilerbond'
        exit 1
    }
    foreach ($pi in $playerItems) {
        if ($pi -notmatch '^(gun|char|head)/[a-z0-9_]{1,30}$') {
            Write-Host "prepare-xbla-assets: '$pi' is not gun/<item>, char/<item> or head/<item>."
            exit 1
        }
    }
    if ($Level) {
        $inv = & $python $viewmodelsTool inventory $Level
        if ($LASTEXITCODE -ne 0) {
            $inv | ForEach-Object { Write-Host $_ }
            exit 1
        }
    }
}
$items = $charItems
if ($Characters) {
    if ($Level -and $items.Count -gt 0) {
        Write-Host 'prepare-xbla-assets: -Characters takes a level OR a list of items, not both.'
        exit 1
    }
    if (-not $Level -and $items.Count -eq 0) {
        Write-Host 'prepare-xbla-assets: -Characters needs a stage or a list:'
        Write-Host '  .\tools\windows\prepare-xbla-assets.ps1 -Level dam -Characters'
        Write-Host '  .\tools\windows\prepare-xbla-assets.ps1 -Characters char/greatguard2,head/headlee'
        exit 1
    }
    foreach ($ci in $items) {
        if ($ci -notmatch '^(char|head)/[a-z0-9_]{1,30}$') {
            Write-Host "prepare-xbla-assets: '$ci' is not char/<item> or head/<item>."
            exit 1
        }
    }
    if ($Level) {
        # A stage the game's tables cannot answer for is refused here, before
        # the source is read at all.
        $inv = & $python $stagechars inventory $Level
        if ($LASTEXITCODE -ne 0) {
            $inv | ForEach-Object { Write-Host $_ }
            exit 1
        }
    }
} elseif ($Level -and -not $Player -and -not $Props -and -not $Background -and -not $Skydome) {
    Write-Host 'prepare-xbla-assets: -Level names the stage whose -Characters or -Player to prepare.'
    Write-Host '  .\tools\windows\prepare-xbla-assets.ps1 -Level dam -Characters'
    exit 1
}

if (-not $Out) { $Out = Join-Path $repo 'worldassets' }
$sources = Join-Path $repo 'texsources'
if (-not $XblaArchive) { $XblaArchive = Join-Path $sources 'xbla-source.7z' }

# The assets this slice prepares. Class and item path only - no source layout,
# no archive name, no acquisition pointer. One entry per named switch, so what
# a run does is exactly what was asked for.
$targets = New-Object System.Collections.Generic.List[object]
if ($DamSkydome)    { $targets.Add([pscustomobject]@{ Class = 'skydome';    Item = 'dam' }) }
if ($Skydome)       { $targets.Add([pscustomobject]@{ Class = 'skydome';    Item = $Level }) }
if ($DamBackground) { $targets.Add([pscustomobject]@{ Class = 'background'; Item = 'dam' }) }
if ($Background)    { $targets.Add([pscustomobject]@{ Class = 'background'; Item = $Level }) }

if (-not (Test-Path -LiteralPath $XblaArchive)) {
    Write-Host ''
    Write-Host 'prepare-xbla-assets: no source.'
    Write-Host "  Looked for: $XblaArchive"
    Write-Host '  The source is user-supplied: Sightline neither ships, locates nor'
    Write-Host '  downloads one. Put your own copy at that path (or pass'
    Write-Host '  -XblaArchive <path>) and run this again.'
    Write-Host ''
    exit 1
}

$stampDir = Join-Path $repo 'build\worldasset'
# A stamp answers for the bundle in ONE output root: "current" means that
# root's file was built from this source by these tools. The default root
# (worldassets\) keeps build\worldasset; any other -Out gets a folder of its
# own, named by its full path, so preparing into a second root never marks
# the first one's older bundles current.
$outFull = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Out).TrimEnd('\')
$defaultOut = (Join-Path $repo 'worldassets').TrimEnd('\')
if ($outFull -ine $defaultOut) {
    $rootKey = (Get-FileHash -InputStream ([IO.MemoryStream]::new(
                   [Text.Encoding]::UTF8.GetBytes($outFull.ToLowerInvariant()))) -Algorithm SHA256).Hash
    $stampDir = Join-Path $repo ('build\worldasset\roots\' + $rootKey.Substring(0, 16).ToLowerInvariant())
}

# A bundle whose source is the one it was built from is already current. The
# stamp lives in gitignored build output, so no fingerprint of a user-supplied
# archive is ever recorded in the tree.
$srcHash = (Get-FileHash -LiteralPath $XblaArchive -Algorithm SHA256).Hash.ToLowerInvariant()
$toolHash = ''
# Every module that decides a byte of the bundle, INCLUDING the container and
# texture readers it borrows from the texture pipeline. Leaving those two out
# is how a decoder repair can land and a stale bundle keep answering for it
# (#68): the source is unchanged, so only the tool hash can notice.
foreach ($t in @('tools\worldasset\prepare.py', 'tools\worldasset\slwa.py',
                 'tools\worldasset\xblaworld.py', 'tools\worldasset\zenith.py',
                 'tools\worldasset\xblachar.py', 'tools\worldasset\viewmodels.py',
                 'tools\worldasset\stageprops.py', 'tools\worldasset\watchhand.py',
                 'tools\worldasset\gameshade.py', 'tools\texpack\uvcarry.py',
                 'tools\texpack\caff.py', 'tools\texpack\xdecode.py')) {
    $toolHash += (Get-FileHash -LiteralPath (Join-Path $repo $t) -Algorithm SHA256).Hash
}
$toolHash = (Get-FileHash -InputStream ([IO.MemoryStream]::new(
                [Text.Encoding]::ASCII.GetBytes($toolHash))) -Algorithm SHA256).Hash.ToLowerInvariant()
# The watch hand's cuffs are read from the user's ROM (the same boundary this
# source lives in): a bundle prepared with or without it, or from another one,
# is a different bundle.
$romFile = Join-Path $repo 'baserom.u.z64'
if ($env:SL_ROM) { $romFile = $env:SL_ROM }
$romHash = 'none'
if (Test-Path -LiteralPath $romFile) {
    $romHash = (Get-FileHash -LiteralPath $romFile -Algorithm SHA256).Hash.ToLowerInvariant()
}
$toolHash = (Get-FileHash -InputStream ([IO.MemoryStream]::new(
                [Text.Encoding]::ASCII.GetBytes("$toolHash $romHash"))) -Algorithm SHA256).Hash.ToLowerInvariant()
$stampNow = "1 $srcHash $toolHash"
# A divergence prepared AS AUTHORED (SL_WA_AS_AUTHORED, tools\worldasset\
# prepare.py) is a different bundle: the list joins the stamp, so setting or
# clearing it prepares again. Unset, the stamp is what it always was.
if ($env:SL_WA_AS_AUTHORED) {
    $stampNow += ' as-authored ' + (($env:SL_WA_AS_AUTHORED.ToUpperInvariant() -split ',' |
                                     ForEach-Object { $_.Trim() } | Where-Object { $_ } |
                                     Sort-Object -Unique) -join ',')
}

# 7-Zip is a DEVELOPER tool here, like the compiler: discovered at runtime,
# never a literal path, and not a runtime dependency of the game.
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
    Write-Host 'prepare-xbla-assets: no 7-Zip to open the source with.'
    Write-Host '  Install 7-Zip, or point SL_SEVENZIP at a 7z.exe. It is a developer'
    Write-Host '  tool for this one step; the game never needs it.'
    Write-Host ''
    exit 1
}

New-Item -ItemType Directory -Force -Path $stampDir | Out-Null

# The archive index is read ONCE, whatever is being prepared. Each item's
# location inside it is DISCOVERED from that index rather than assumed, so
# nothing here describes any particular source layout.
Write-Host ''
Write-Host "world assets: indexing $XblaArchive"
$index = & $sz l $XblaArchive -slt -ba | Select-String -SimpleMatch 'Path = '
$paths = $index | ForEach-Object { $_.Line.Substring($_.Line.IndexOf('Path = ') + 7) }

# -All: the campaign plan, from the runtime's own tables and this index
# (tools\worldasset\campaign.py) - the unique backgrounds and domes become
# the targets below, and the stages the per-stage steps after them.
$stageList = @($Level)
if ($All) {
    $indexFile = Join-Path $stampDir 'source-index.txt'
    $paths | Set-Content -LiteralPath $indexFile -Encoding utf8
    $planJson = & $python (Join-Path $repo 'tools\worldasset\campaign.py') plan --index $indexFile
    if ($LASTEXITCODE -ne 0) {
        Write-Host 'prepare-xbla-assets: the campaign plan could not be read; nothing was prepared.'
        exit 1
    }
    $plan = ($planJson -join "`n") | ConvertFrom-Json
    $stageList = @($plan.stages)
    foreach ($b in @($plan.backgrounds)) { $targets.Add([pscustomobject]@{ Class = 'background'; Item = $b }) }
    foreach ($s in @($plan.skydomes))    { $targets.Add([pscustomobject]@{ Class = 'skydome';    Item = $s }) }
    $Background = [switch]$true
    $Characters = [switch]$true
    $Props      = [switch]$true
    $Player     = [switch]$true
    $withBody   = $true
    Write-Host ''
    Write-Host ("world assets: -All - {0} stages ({1}); {2} backgrounds, {3} domes" -f
                $stageList.Count, ($stageList -join ' '), @($plan.backgrounds).Count, @($plan.skydomes).Count)
    if (@($plan.absent_skydomes).Count -gt 0) {
        Write-Host ("              no dome in the source for {0} - those stages keep the game's sky" -f
                    (@($plan.absent_skydomes) -join ' '))
    }
}

$done = New-Object System.Collections.Generic.List[string]
foreach ($t in $targets) {
    $class = $t.Class
    $item  = $t.Item
    $outFile   = Join-Path $Out "xbla\$class\$item.slwa"
    $stampFile = Join-Path $stampDir "$class-$item.stamp"

    if (-not $Force -and (Test-Path -LiteralPath $outFile) -and (Test-Path -LiteralPath $stampFile) -and
        ((Get-Content -Raw -LiteralPath $stampFile).Trim() -eq $stampNow)) {
        Write-Host ''
        Write-Host "  $class/$item : source unchanged (sha256 verified) and current - nothing to do."
        Write-Host '                Pass -Force to convert it again anyway.'
        $done.Add("$class/$item|$outFile")
        continue
    }

    $suffix = "\$class\$item\"
    $wanted = New-Object System.Collections.Generic.List[string]
    foreach ($p in $paths) {
        if (($p.EndsWith('.bin') -or $p.EndsWith('.rba')) -and $p.Contains($suffix) -and
            ($p.Substring(0, $p.LastIndexOf('\'))).EndsWith("\$class\$item")) {
            $wanted.Add($p)
        }
    }
    if ($wanted.Count -eq 0) {
        Write-Host "prepare-xbla-assets: that source holds no $class/$item."
        Write-Host '  It is refused rather than guessed at. Check that it is the'
        Write-Host '  source this pipeline was made against.'
        exit 1
    }

    $scratch = Join-Path $repo "build\worldasset\$class-$item"
    New-Item -ItemType Directory -Force -Path $scratch | Out-Null
    $list = Join-Path $stampDir "include-$class-$item.txt"
    ($wanted | Sort-Object -Unique) | Set-Content -LiteralPath $list -Encoding ascii
    Write-Host ''
    Write-Host "  $class/$item : $($wanted.Count) file(s) -> $scratch"
    & $sz x $XblaArchive "-o$scratch" "@$list" -y -bso0 -bsp0 | Out-Null
    if ($LASTEXITCODE -ne 0) {
        Write-Host "prepare-xbla-assets: 7-Zip failed (exit $LASTEXITCODE) on $XblaArchive."
        Write-Host '  An unreadable or partial source is refused rather than guessed at.'
        exit 1
    }

    # The ROM, when there is one: a background's D-033 reads the game's own
    # room shades from it (tools\worldasset\gameshade.py).
    $romArgs = @()
    if (Test-Path -LiteralPath $romFile) { $romArgs = @('--rom', $romFile) }
    & $python (Join-Path $repo 'tools\worldasset\prepare.py') $class --item $item `
              --tree $scratch --out $Out @romArgs
    $rc = $LASTEXITCODE
    if (-not $KeepScratch) {
        Remove-Item -LiteralPath $scratch -Recurse -Force -ErrorAction SilentlyContinue
    }
    if ($rc -ne 0) {
        Write-Host ''
        Write-Host "prepare-xbla-assets: nothing was written for $class/$item."
        Write-Host ''
        exit $rc
    }
    Set-Content -LiteralPath $stampFile -Encoding ascii -Value $stampNow
    $done.Add("$class/$item|$outFile")
}

# ---- #70 CHARACTERS ---------------------------------------------------------
# One pass over the source for the whole set: the index above is handed over,
# the items that are not current are extracted TOGETHER (a source may be one
# solid block, where every separate extraction decompresses it again), each
# is converted and stamped exactly as a bundle above is, and every part of
# the set gets a line - PREPARED, CURRENT, SOURCE ABSENT or CONVERSION
# REFUSED with its reason.
$charRc = 0
$playerRc = 0
$propsRc = 0
# Every step that ended non-zero under -All, for the closing summary.
$stageFailures = New-Object System.Collections.Generic.List[string]
if ($Characters -or $Player -or $Props) {
    $indexFile = Join-Path $stampDir 'source-index.txt'
    $paths | Set-Content -LiteralPath $indexFile -Encoding utf8
}
# One pass per stage: the stage -Level names, or under -All every stage of the
# plan in the runtime table's order. Each step below is the same call a
# single-stage run makes.
$stageNo = 0
foreach ($lv in $stageList) {
    $stageNo++
    if ($All) {
        Write-Host ''
        Write-Host ("==== [{0}/{1}] {2}" -f $stageNo, $stageList.Count, $lv)
    }

    if ($Characters) {
        $what = $lv
        if (-not $what) { $what = 'items' }
        $pyArgs = @($stagechars, 'prepare')
        if ($lv) { $pyArgs += @('--level', $lv) }
        else     { $pyArgs += @('--items', ($items -join ',')) }
        $pyArgs += @('--index', $indexFile, '--archive', $XblaArchive, '--sevenzip', $sz,
                     '--out', $Out, '--stamps', $stampDir, '--stamp', $stampNow,
                     '--scratch', (Join-Path $stampDir 'characters'),
                     '--report', (Join-Path $stampDir "characters-$what.json"))
        if ($Force)       { $pyArgs += '--force' }
        if ($KeepScratch) { $pyArgs += '--keep-scratch' }
        & $python @pyArgs
        if ($LASTEXITCODE -ne 0) {
            $charRc = $LASTEXITCODE
            $stageFailures.Add("$lv characters (exit $LASTEXITCODE)")
        }
    }

    # ---- #70 THE PLAYER'S ITEMS ----------------------------------------------
    # The same one-pass, stamped, per-row reporting as the characters above,
    # for every first-person model the player can see and, with -Player, the
    # external body. A row whose outcome is recorded under this stamp -
    # prepared, refused or absent - is answered from that record, so an
    # unchanged rerun extracts and converts nothing.
    if ($Player) {
        $pyArgs = @($viewmodelsTool, 'prepare')
        if ($lv) { $pyArgs += @('--level', $lv); if ($withBody) { $pyArgs += '--player' } }
        else     { $pyArgs += @('--items', ($playerItems -join ',')) }
        $pyArgs += @('--index', $indexFile, '--archive', $XblaArchive, '--sevenzip', $sz,
                     '--out', $Out, '--stamps', $stampDir, '--stamp', $stampNow,
                     '--scratch', (Join-Path $stampDir 'player'))
        if (Test-Path -LiteralPath $romFile) { $pyArgs += @('--rom', $romFile) }
        if ($Force)       { $pyArgs += '--force' }
        if ($KeepScratch) { $pyArgs += '--keep-scratch' }
        & $python @pyArgs
        if ($LASTEXITCODE -ne 0) {
            $playerRc = $LASTEXITCODE
            $stageFailures.Add("$lv player items (exit $LASTEXITCODE)")
        }
    }

    # ---- #70 THE STAGE'S RIGID PROPS -----------------------------------------
    # The same one-pass, stamped, per-row reporting, for the models of the
    # stage's standard props and single monitors.
    if ($Props) {
        if (-not $lv) {
            Write-Host 'prepare-xbla-assets: -Props takes a stage:'
            Write-Host '  .\tools\windows\prepare-xbla-assets.ps1 -Level dam -Props'
            exit 1
        }
        $pyArgs = @((Join-Path $repo 'tools\worldasset\stageprops.py'), 'prepare', '--level', $lv,
                    '--index', $indexFile, '--archive', $XblaArchive, '--sevenzip', $sz,
                    '--out', $Out, '--stamps', $stampDir, '--stamp', $stampNow,
                    '--scratch', (Join-Path $stampDir 'props'))
        if (Test-Path -LiteralPath $romFile) { $pyArgs += @('--rom', $romFile) }
        if ($Force)       { $pyArgs += '--force' }
        if ($KeepScratch) { $pyArgs += '--keep-scratch' }
        & $python @pyArgs
        if ($LASTEXITCODE -ne 0) {
            $propsRc = $LASTEXITCODE
            $stageFailures.Add("$lv props (exit $LASTEXITCODE)")
        }
    }
}

# ---- #68 THE REMASTER'S STAGE FOG ------------------------------------------
# The replacement background's own programs fog toward a per-environment
# colour and range that live in the source's EXECUTABLE, not in any item. The
# executable is read from the same user-supplied source, locally, by
# tools\worldasset\xexenv.py, which writes one small generated file beside the
# bundles (xbla\environment.slen). Nothing about the executable is kept or
# recorded. A source without one, or one this reader refuses, degrades to the
# unfogged replacement with a line saying so - never a failed preparation.
if ($DamBackground -or $Background) {
    $envOut   = Join-Path $Out 'xbla\environment.slen'
    $envStamp = Join-Path $stampDir 'environment.stamp'
    $envTool  = ''
    # The extractor decides the output, and so do the ORIGINAL rows it
    # recognises the table by - they are read from the decomp source.
    foreach ($t in @('tools\worldasset\xexenv.py', 'src\game\bgfog.c', 'src\bondconstants.h')) {
        $envTool += (Get-FileHash -LiteralPath (Join-Path $repo $t) -Algorithm SHA256).Hash
    }
    $envTool = (Get-FileHash -InputStream ([IO.MemoryStream]::new(
                  [Text.Encoding]::ASCII.GetBytes($envTool))) -Algorithm SHA256).Hash.ToLowerInvariant()
    $envNow = "1 $srcHash $envTool"
    Write-Host ''
    if (-not $Force -and (Test-Path -LiteralPath $envOut) -and (Test-Path -LiteralPath $envStamp) -and
        ((Get-Content -Raw -LiteralPath $envStamp).Trim() -eq $envNow)) {
        Write-Host '  environment : source unchanged (sha256 verified) and current - nothing to do.'
        $done.Add("environment|$envOut")
    } else {
        $exes = @($paths | Where-Object { $_.ToLowerInvariant().EndsWith('\default.xex') -or
                                          $_.ToLowerInvariant() -eq 'default.xex' })
        if ($exes.Count -ne 1) {
            Write-Host "  environment : the source holds $($exes.Count) executables where one was"
            Write-Host '                expected; the replacement background draws without the'
            Write-Host "                remaster's stage fog."
        } else {
            $envScratch = Join-Path $repo 'build\worldasset\environment'
            New-Item -ItemType Directory -Force -Path $envScratch | Out-Null
            & $sz x $XblaArchive "-o$envScratch" $exes[0] -y -bso0 -bsp0 | Out-Null
            $xex = Join-Path $envScratch $exes[0]
            if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $xex)) {
                Write-Host '  environment : the executable could not be extracted; the replacement'
                Write-Host "                background draws without the remaster's stage fog."
            } else {
                & $python (Join-Path $repo 'tools\worldasset\xexenv.py') --xex $xex --out $Out
                if ($LASTEXITCODE -eq 0) {
                    Set-Content -LiteralPath $envStamp -Encoding ascii -Value $envNow
                    $done.Add("environment|$envOut")
                } else {
                    # A refusal must not leave an older derivation answering.
                    Remove-Item -LiteralPath $envOut -Force -ErrorAction SilentlyContinue
                    Remove-Item -LiteralPath $envStamp -Force -ErrorAction SilentlyContinue
                }
            }
            # The executable is working input only: never kept.
            Remove-Item -LiteralPath $envScratch -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}

Write-Host ''
foreach ($d in $done) {
    $name, $f = $d.Split('|')
    if (Test-Path -LiteralPath $f) {
        $kb = [int]((Get-Item -LiteralPath $f).Length / 1024)
        Write-Host ("  {0,-18} {1,9} KB  {2}" -f $name, $kb, $f)
    } else {
        Write-Host ("  {0,-18} not prepared" -f $name)
    }
}
if ($All) {
    Write-Host ''
    Write-Host ("prepare-xbla-assets -All: {0} stages, {1} backgrounds, {2} domes." -f
                $stageList.Count, @($plan.backgrounds).Count, @($plan.skydomes).Count)
    if ($stageFailures.Count -gt 0) {
        Write-Host '  Steps that did not finish cleanly (see their lines above; a refused or'
        Write-Host '  absent item keeps the game''s own model):'
        foreach ($f in $stageFailures) { Write-Host "    $f" }
        Write-Host ''
        exit 1
    }
    Write-Host '  Every step finished cleanly.'
    Write-Host ''
    Write-Host 'Launch with .\tools\windows\play.ps1 -Level dam -Presentation XBLA'
    Write-Host 'Texture packs are a separate step: .\tools\windows\prepare-textures.ps1 -Xbla'
    Write-Host ''
    exit 0
}
if ($charRc -ne 0) {
    Write-Host 'prepare-xbla-assets: a character item was refused; see its line above.'
    Write-Host '  Every other item was prepared, and the refused part draws as the game''s.'
    Write-Host ''
    exit $charRc
}
if ($playerRc -ne 0) {
    Write-Host 'prepare-xbla-assets: a named player item was refused or absent; see its line above.'
    Write-Host ''
    exit $playerRc
}
if ($propsRc -ne 0) {
    Write-Host 'prepare-xbla-assets: the stage''s props could not be derived; see above.'
    Write-Host ''
    exit $propsRc
}
Write-Host ''
$launchLevel = 'dam'
if ($Level) { $launchLevel = $Level }
# A background item the runtime's stage table names for one stage launches as
# that stage (src/native/sl_world_assets.c, as gameshade._table_stages reads
# it): the source's `temple` is Egyptian, while the launcher's `temple` is the
# multiplayer Temple.
$waSrc = Join-Path $repo 'src\native\sl_world_assets.c'
if ($Level -and (Test-Path -LiteralPath $waSrc)) {
    $tbl = [regex]::Matches((Get-Content -Raw -LiteralPath $waSrc), 'case\s+LEVELID_(\w+)\s*:[^;\n]*"' + [regex]::Escape($Level) + '"')
    if ($tbl.Count -eq 1) { $launchLevel = $tbl[0].Groups[1].Value.ToLower() }
}
Write-Host "Launch with .\tools\windows\play.ps1 -Level $launchLevel -Cheats invincible,invisible,maxammo"
Write-Host 'then F5 cycles ORIGINAL -> COMMUNITY HD -> XBLA. World assets follow the'
Write-Host 'presentation; this root is found on its own.'
Write-Host ''
exit 0
