<#
.SYNOPSIS
    Launch the Windows build as something you can play.

.DESCRIPTION
    Run from an ordinary Windows PowerShell prompt at the repository root:

        .\tools\windows\play.ps1                 # the game's own front end
        .\tools\windows\play.ps1 -Level dam -Difficulty 2   # straight into a level
        .\tools\windows\play.ps1 -MeasurementMode           # THE measurement launch

    Everything here is a default, not a policy: each value is overridable, and
    the settings it chooses are the ones a player wants rather than the ones
    trace replay wants (a window, real-time pacing, no frame limit, live
    keyboard and mouse).

    No assets are read from the repo. The level data, models and textures all
    come out of your own ROM at runtime, which is why SL_ROM is required and
    why the ROM is never committed.

.PARAMETER Level
    Level name, and DEVELOPER direct boot. Omit it and the game boots the way
    the cartridge does, through its own title sequence and menus; name one and
    it loads that stage immediately instead.

    Resolved to a boot stage through tools/native/levelstage.py - the one
    resolver the demo, the health gate and the determinism corpus all share.
    Two copies of that lookup is how a script came to pass a name the binary
    never reads. Overridable with SL_LEVEL.

.PARAMETER Difficulty
    0..3. Applies to direct boot only; the front end has its own difficulty
    select. Overridable with SL_DIFFICULTY.

.PARAMETER Size
    The INITIAL window size, WxH. Overridable with SL_WINDOW_SIZE. The
    ASPECT RATIO setting (OPTIONS -> SETTINGS -> DISPLAY: 4:3 / 16:9 / 32:9,
    persisted in config.ini) then sets the window's WIDTH from its height -
    960x720 stays 960x720 at 4:3, becomes 1280x720 at 16:9 and 2560x720 at
    32:9, live when the setting changes and at launch from the saved
    setting. So this names the height (and the 4:3 width); the aspect wins
    on the width. Never a stretch, never a strip inside the old window.

.PARAMETER Quiet
    Skip the controls summary.

.PARAMETER NoConsoleCapture
    A playable, LOW-OVERHEAD launch with no run capture and no extra console.

    IT IS NOT A MEASUREMENT LAUNCH. An earlier revision of this file claimed it
    "launches the way automation does"; that claim was false and is withdrawn.
    It differs from the known-good measurement launch in four ways that were
    never controlled: it passes -NoNewWindow (so the child gets no console of
    its own, where the measurement child got CREATE_NEW_CONSOLE), it writes the
    player's REAL EEPROM rather than a scratch one, it names no SL_FRAMES, and
    it inherits whatever SL_* variables happen to be in the shell. Use
    -MeasurementMode for the launch automation reproduces.

    What this flag does do, relative to the default path: the window still
    opens and is played normally, and only two things change:

      * run capture is disabled (SL_RUN=0), so stderr is NOT reopened onto a
        line-buffered file in the run directory - see sl_main.c:294;
      * stdout and stderr are redirected to files, so `isatty` is false on
        both and the binary takes every no-terminal branch it has.

    Named for the two mechanisms rather than for "debug": nothing here builds
    or runs a different binary, and no game setting changes. Every other
    launch value - ROM, window flag, window size, EEPROM path, and the absence
    of SL_BOOT_LEVEL when no -Level is given - is established by the same
    table the default path uses, so the two are a controlled comparison.

    THE COST: this run is not recorded. The capture path exists to preserve
    playthroughs that cannot be recreated, so use the flag to compare, not to
    play. The default is unchanged and still records.

    Where the redirected output went is printed BEFORE the redirect happens.
    The location is derived at runtime from %LOCALAPPDATA% (falling back to
    %USERPROFILE%\AppData\Local, then %TEMP%), never from a literal path, and
    SL_NOCAPTURE_DIR overrides it - the same shape as SL_SAVE overriding the
    EEPROM path above.

.PARAMETER MeasurementMode
    THE canonical measurement launch. One command, used in manual play and by
    automation alike, so that "it rotates correctly in play" and "it rotates
    correctly in automation" are statements about the same process creation.

    This mode exists because they were NOT the same. A measurement run that
    reached NINTENDO at frames=501 / 8.327 s was reconstructed as: window on,
    960x720, a SCRATCH EEPROM, SL_FRAMES=1600, SL_SHOT explicitly removed, and
    Start-Process with the streams redirected, -PassThru and -Wait - and
    NOTHING ELSE. In particular NO -NoNewWindow, so the child is created with
    its own console.

    THAT EXTRA CONSOLE WINDOW IS EXPECTED AND MUST NOT BE "FIXED". It is part
    of the process creation being reproduced. Whether it matters is a question
    for a later round; removing it here would once again make the manual
    launch and the automated launch two different things, which is the entire
    defect this mode was added to close.

    Every launch value is ESTABLISHED rather than inherited: SL_ROM, SL_WINDOW,
    SL_WINDOW_SIZE, SL_RUN, SL_EEPROM_RW, SL_FRAMES are set, SL_SHOT is
    removed, and any OTHER SL_* variable sitting in the shell is cleared for
    the child and named in the manifest as it goes. Ambient SL_LEVEL and
    SL_WINDOW_SIZE are ignored too - only an explicit -Level or -Size is
    honoured - because a debug flag left in one shell and not the other is
    exactly how two runs come to disagree. The prior environment is restored
    on the way out, as every other path here does.

    SL_FRAMES=1600 is the known-good value and is deliberate: naming SL_FRAMES
    suppresses the windowed "run until closed" rule (sl_ultra_shim.c:916), so
    the run EXITS at frame 1600 - about 27 s at 60 fps. The ROM oracle puts
    Nintendo near 8.3 s, around frames 311-811, comfortably inside that.

    The EEPROM is a SCRATCH file, derived at runtime beside the redirected
    streams. The player's real save is never opened, and automated and manual runs
    boot from the same state.

    Like -NoConsoleCapture, this run is NOT recorded (SL_RUN=0).

.PARAMETER Cheats
    DEVELOPER: GoldenEye's own cheats, switched on for the level the way the
    cheat menu switches them on. A comma-separated list (or several values)
    by the menu's names
    (allguns, maxammo, invincible, infammo, 2xhealth, dkmode, paintball,
    tiny, turbo, ...) or by CHEAT_IDS number; src/native/sl_cheat.c holds
    the table and the mechanism. Direct boot only. A cheated mission is not
    recorded as completed - that is the cartridge's own rule.

        .\tools\windows\play.ps1 -Level surface -Cheats allguns,maxammo

.PARAMETER Weapon
    DEVELOPER: the item Bond holds in his right hand once the level is in
    first person, by in-game name (pp7, dd44, klobb, kf7, zmg, d5k, phantom,
    ar33, rcp90, shotgun, autoshotgun, sniper, cougar, goldengun, laser,
    grenadelauncher, rocketlauncher, grenade, timedmine, proximitymine,
    remotemine, taser, ...), by ITEM_IDS stem, or by number. An item the
    level has not handed out is added to the inventory with a full load,
    so -Cheats is not required for it. Combine with -TeleportMark to stand
    at a playtest mark holding the weapon the mark was taken with:

        .\tools\windows\play.ps1 -Level surface -Weapon sniper `
            -TeleportMark C:\Users\me\.sightline\runs\<run>\mark-001.txt

.PARAMETER Presentation
    DEVELOPER: start on a named PRESENTATION (#68) - ORIGINAL, COMMUNITY or
    XBLA - so a test does not need a manual environment export or a trip
    through the menus. It writes the one canonical stored key, `presentation`,
    into the config this run will use (SL_CONFIG when that names one, the
    player's config.ini otherwise), leaving every other line alone. That is
    the SAME value F5 steps, so the run starts where you asked and F5 keeps
    working from there.

    ONE SELECTION MOVES EVERYTHING. The presentation resolves every visual
    provider: ORIGINAL = the game's own artwork and no external world assets;
    COMMUNITY = the Community HD textures, still the game's own world; XBLA =
    the XBLA textures AND the XBLA world assets, so the Dam's skydome comes
    with them. The normal path needs no -WorldAssets at all:

        .\tools\windows\play.ps1 -Level dam -Cheats invincible,invisible,maxammo

    then F5 (or the pad's BACK) cycles ORIGINAL -> COMMUNITY HD -> XBLA ->
    ORIGINAL with the whole picture following.

.PARAMETER WorldAssets
    DIAGNOSTIC / ADVANCED, and not needed for ordinary play. It pins the
    WORLD-ASSET PROVIDER ALONE (#68) ahead of the presentation:

        FOLLOW   (the default) the presentation decides, and SL_WORLD_ASSETS
                 is CLEARED for the child so a variable left in the shell
                 cannot quietly make the session look mixed
        OFF      pin the provider dark whatever the presentation selects
        XBLA     pin the xbla set lit whatever the presentation selects

    OFF and XBLA can produce a MIXED state - XBLA textures with no skydome,
    or the skydome over the game's own artwork - which is exactly what the
    coordination round removed from the player's reach. When they do, this
    launcher says so before the run and the binary says so again on stderr.
    Use them to isolate one provider, never to play.

    The root is found the same way the texture pack root is:
    SL_WORLDASSET_ROOT if set, otherwise the checkout's own worldassets\,
    otherwise the player-data ladder - and it is handed to the child whenever
    it exists, so the presentation-driven path finds a prepared asset without
    any switch. Prepare the one asset this slice carries with
    .\tools\windows\prepare-xbla-assets.ps1 -DamSkydome.
#>
[CmdletBinding()]
param(
    [string]$Level,
    [string]$Difficulty,
    [string]$Size,
    [string]$TeleportMark,
    [string[]]$Cheats,
    [string]$Weapon,
    [ValidateSet('ORIGINAL', 'COMMUNITY', 'XBLA')]
    [string]$Presentation,
    [ValidateSet('FOLLOW', 'OFF', 'XBLA')]
    [string]$WorldAssets = 'FOLLOW',
    [switch]$Quiet,
    [switch]$NoConsoleCapture,
    [switch]$MeasurementMode
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

# NO LEVEL MEANS THE GAME'S OWN FRONT END, and the default is deliberately
# empty rather than 'facility'.
#
# SL_BOOT_LEVEL is a DEVELOPER seam: it pokes g_StageNum before mainproc runs
# (sl_ultra_shim.c:1897) so boss.c takes its "not the title" branch and loads a
# stage directly (boss.c:373). Setting it unconditionally meant an ordinary
# launch could never see the title sequence, the file select, mission select or
# difficulty select - the whole front end was unreachable by playing, not by
# choice. Naming a level still asks for it; that is what the flag is for.
if (-not $Level)      { $Level      = $env:SL_LEVEL }
if (-not $Difficulty) { $Difficulty = $env:SL_DIFFICULTY;  if (-not $Difficulty) { $Difficulty = '1' } }
if (-not $Size)       { $Size       = $env:SL_WINDOW_SIZE; if (-not $Size)       { $Size       = '960x720' } }

if ($MeasurementMode -and $NoConsoleCapture) {
    Write-Host 'sightline: -MeasurementMode and -NoConsoleCapture are different launches.'
    Write-Host '  Pick one. -MeasurementMode is the canonical measurement launch;'
    Write-Host '  -NoConsoleCapture is a playable low-overhead launch.'
    exit 1
}
# AMBIENT VALUES DO NOT REACH A MEASUREMENT RUN. SL_LEVEL and SL_WINDOW_SIZE
# are read above as ordinary defaults, which is right for playing and wrong
# here: a variable left in one shell and not the other is how the manual run
# and the automated run silently stopped being the same run. Only an explicitly
# passed -Level or -Size is honoured.
if ($MeasurementMode) {
    if (-not $PSBoundParameters.ContainsKey('Level')) { $Level = $null }
    if (-not $PSBoundParameters.ContainsKey('Size'))  { $Size  = '960x720' }
}
$directBoot = [bool]$Level

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

$stage = $null
if ($directBoot) {
    $stage = & $python (Join-Path $repo 'tools\native\levelstage.py') $Level
    if ($LASTEXITCODE -ne 0 -or -not $stage) {
        Write-Host "sightline: unknown level '$Level'."
        Write-Host '  Known names are the keys in tools/trace/sltrace/levelboot.py'
        Write-Host '  (facility, dam, runway, surface, bunker1, silo, archives, ...).'
        exit 1
    }
    $stage = "$stage".Trim()
}

$exe = Join-Path $repo 'build\win32\sightline.exe'
if (-not (Test-Path $exe)) {
    & (Join-Path $PSScriptRoot 'build.ps1')
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $exe)) {
        Write-Host 'sightline: build failed; not launching.'
        exit 1
    }
}

# The cartridge save. Without one the game starts from defaults every launch
# and nothing changed in the options survives. Lives OUTSIDE the repo, under
# %LOCALAPPDATA%: it is player data, and rule 2 keeps ROM-derived bytes out of
# the tree regardless.
$save = $env:SL_SAVE
if (-not $save) {
    $base = $env:LOCALAPPDATA
    if (-not $base) { $base = Join-Path $env:USERPROFILE 'AppData\Local' }
    $save = Join-Path $base 'sightline\eeprom.bin'
}
$saveDir = Split-Path -Parent $save
if ($saveDir -and -not (Test-Path $saveDir)) {
    New-Item -ItemType Directory -Force -Path $saveDir | Out-Null
}

# TEXTURE SETS (#47). The binary's own contract is unchanged and no source
# path is compiled into it: it reads SL_TEXPACK_ROOT when that is set, and
# otherwise the player-data ladder, %LOCALAPPDATA%\sightline\assets\texpacks.
# What a SOURCE CHECKOUT needs on top of that is for a developer who has run
# tools\windows\prepare-textures.ps1 to SEE the pack without exporting
# anything, which is what this does - the repository's own texpacks\ is
# handed to the child when it is there. Same shape as $rom above: an explicit
# value wins, otherwise the checkout supplies one.
#
# THIS IS THE DEFECT THAT FAILED #47's FIRST ROUND. The prepared packs lived
# outside the tree and only an explicitly exported SL_TEXPACK_ROOT found
# them. An ordinary `.\tools\windows\play.ps1` therefore read a pack root
# that did not exist, every texture fell back to ORIGINAL - reported in playtest as "I don't notice
# any changes with the graphics or textures" - while the automated runs that
# did set the variable saw replacements and called it working. The two were
# never the same launch. They are now.
$texpack = $env:SL_TEXPACK_ROOT
$texpackWhy = 'SL_TEXPACK_ROOT'
if (-not $texpack) {
    $devPack = Join-Path $repo 'texpacks'
    if (Test-Path $devPack) {
        $texpack = $devPack
        $texpackWhy = 'this checkout'
    }
}
if (-not $texpack) {
    $tpBase = $env:LOCALAPPDATA
    if (-not $tpBase -and $env:USERPROFILE) { $tpBase = Join-Path $env:USERPROFILE 'AppData\Local' }
    if ($tpBase) { $texpackShown = Join-Path $tpBase 'sightline\assets\texpacks' }
    else         { $texpackShown = '(no player-data directory)' }
    $texpackWhy = 'player data'
} else {
    $texpackShown = $texpack
}

# WORLD ASSETS (#68). The same shape as the pack root above and for the same
# reason. The two providers remain separate MECHANISMS - additive world
# geometry keyed by asset class and item path on one side, per-texture
# substitution on the other - but since 2026-09-23 they are no longer selected
# separately: the PRESENTATION selects both, and this root is handed over
# whenever it exists so the presentation-driven path can find a prepared asset
# with no switch at all. -WorldAssets is a diagnostic pin, below.
$waRoot = $env:SL_WORLDASSET_ROOT
$waWhy = 'SL_WORLDASSET_ROOT'
if (-not $waRoot) {
    $devWa = Join-Path $repo 'worldassets'
    if (Test-Path $devWa) { $waRoot = $devWa; $waWhy = 'this checkout' }
}
if (-not $waRoot) {
    $waBase = $env:LOCALAPPDATA
    if (-not $waBase -and $env:USERPROFILE) { $waBase = Join-Path $env:USERPROFILE 'AppData\Local' }
    if ($waBase) { $waShown = Join-Path $waBase 'sightline\assets\worldassets' }
    else         { $waShown = '(no player-data directory)' }
    $waWhy = 'player data'
} else {
    $waShown = $waRoot
}
# THE PRESENTATION (#68, 2026-09-23). ONE canonical stored key, `presentation`,
# in the config this run will use. The launcher reads it to report what the run
# will look like and what it needs prepared, and -Presentation writes it - the
# same value F5 steps, so there is no second authority and no environment
# export to remember.
$cfgPath = $env:SL_CONFIG
if (-not $cfgPath) {
    $cfgBase = $env:LOCALAPPDATA
    if (-not $cfgBase -and $env:USERPROFILE) { $cfgBase = Join-Path $env:USERPROFILE 'AppData\Local' }
    if ($cfgBase) { $cfgPath = Join-Path $cfgBase 'sightline\config.ini' }
}

# READ IT. `presentation` wins; a #47-era config carrying only `textures` reads
# through that, exactly as the binary's own migration does, so the line this
# launcher prints agrees with the run.
$presId = 0
$presWhy = 'default'
if ($cfgPath -and (Test-Path -LiteralPath $cfgPath)) {
    $m = Select-String -LiteralPath $cfgPath -Pattern '^\s*presentation\s*=\s*([0-9]+)' |
         Select-Object -Last 1
    if ($m) {
        $presId = [int]$m.Matches[0].Groups[1].Value
        $presWhy = 'presentation'
    } else {
        $m = Select-String -LiteralPath $cfgPath -Pattern '^\s*textures\s*=\s*([0-9]+)' |
             Select-Object -Last 1
        if ($m) {
            $presId = [int]$m.Matches[0].Groups[1].Value
            $presWhy = 'legacy textures key, migrated at startup'
        }
    }
}

# WRITE IT, when asked. One line replaced or appended; every other line -
# bindings included - is kept byte for byte.
if ($Presentation) {
    $want = @{ 'ORIGINAL' = 0; 'COMMUNITY' = 1; 'XBLA' = 2 }[$Presentation]
    if (-not $cfgPath) {
        Write-Host 'sightline: -Presentation has no config to write (no SL_CONFIG, no player-data directory).'
        exit 1
    }
    $cfgDir = Split-Path -Parent $cfgPath
    if ($cfgDir -and -not (Test-Path $cfgDir)) { New-Item -ItemType Directory -Force -Path $cfgDir | Out-Null }
    if (Test-Path -LiteralPath $cfgPath) { $lines = @(Get-Content -LiteralPath $cfgPath) }
    else { $lines = @('# sightline settings', 'version=1') }
    $done = $false
    $out = @()
    foreach ($l in $lines) {
        if ($l -match '^\s*presentation\s*=') {
            if (-not $done) { $out += "presentation=$want"; $done = $true }
        } else {
            $out += $l
        }
    }
    if (-not $done) { $out += "presentation=$want" }
    Set-Content -LiteralPath $cfgPath -Value $out -Encoding utf8
    $presId = $want
    $presWhy = '-Presentation'
}

if ($presId -lt 0 -or $presId -gt 2) { $presId = 0; $presWhy = 'out of range, read as ORIGINAL' }
$presName = @('ORIGINAL', 'COMMUNITY HD', 'XBLA')[$presId]

# WHAT THE PRESENTATION SELECTS, and one line each when the thing it selects
# is not prepared. Never when ORIGINAL is selected, which reads nothing.
if ($presId -ne 0 -and -not $Quiet) {
    $setDir = Join-Path $texpackShown @('community', 'xbla')[$presId - 1]
    if (-not (Test-Path -LiteralPath $setDir)) {
        Write-Host ""
        Write-Host "PRESENTATION is $presName and no texture pack is prepared ($setDir)."
        Write-Host "  Run .\tools\windows\prepare-textures.ps1 to build one from your own"
        Write-Host "  sources. Until then every texture draws the game's own artwork."
    }
}
if ($presId -eq 2 -and $WorldAssets -ne 'OFF' -and -not $Quiet) {
    $slice = Join-Path $waShown 'xbla\skydome\dam.slwa'
    if (-not (Test-Path -LiteralPath $slice)) {
        Write-Host ""
        Write-Host "PRESENTATION is XBLA and no world asset is prepared ($waShown)."
        Write-Host '  Prepare the one this slice carries with:'
        Write-Host '    .\tools\windows\prepare-xbla-assets.ps1 -DamSkydome'
        Write-Host '  Until then the Dam draws its own sky and every other stage is unchanged.'
    }
}

# THE MIXED-STATE WARNING. -WorldAssets pins one provider ahead of the
# presentation; when the two disagree the run is a mixed picture ON PURPOSE,
# and saying so here is what keeps it from being mistaken for the coordinated
# one. The binary repeats it on stderr from the provider itself.
$waDerived = if ($presId -eq 2) { 'XBLA' } else { 'OFF' }
if ($WorldAssets -ne 'FOLLOW' -and $WorldAssets -ne $waDerived -and -not $Quiet) {
    Write-Host ""
    Write-Host "MIXED STATE: -WorldAssets $WorldAssets pins the world-asset provider,"
    Write-Host "  but PRESENTATION $presName selects $waDerived. This is a diagnostic"
    Write-Host "  launch, not what a player can reach. Drop -WorldAssets for the"
    Write-Host "  coordinated picture."
}
# A variable left in the shell is the same hazard with no switch to blame it
# on, so the default path CLEARS it for the child and says it did.
$waClearedStale = $false
if ($WorldAssets -eq 'FOLLOW' -and $env:SL_WORLD_ASSETS) {
    $waClearedStale = $true
    if (-not $Quiet) {
        Write-Host ""
        Write-Host "SL_WORLD_ASSETS was set in this shell ('$env:SL_WORLD_ASSETS') and has been"
        Write-Host "  CLEARED for this run: the presentation decides. Pass -WorldAssets to pin it."
    }
}

if (-not $Quiet) {
    Write-Host ""
    if ($directBoot) {
        Write-Host "Sightline - direct boot: $Level (stage $stage), difficulty $Difficulty"
    } else {
        Write-Host "Sightline - booting to the title screen"
    }
    Write-Host "Presentation: $presName ($presWhy). World assets: $(if ($WorldAssets -eq 'FOLLOW') { "$waDerived, from the presentation" } else { "$WorldAssets, PINNED by -WorldAssets" })."
    Write-Host ""
    Write-Host "  mouse         CLICK IN THE WINDOW to capture the pointer"
    Write-Host "  move          W A S D  /  left stick"
    Write-Host "  look          mouse    /  right stick"
    Write-Host "  fire          left mouse, R1 or R2   aim  right mouse, Q, L1 or L2"
    Write-Host "  interact      E                      reload  R"
    Write-Host "  crouch        Left Ctrl (hold)"
    Write-Host "  weapons       1 / wheel up = previous, 2 / wheel down = next"
    Write-Host "  scope zoom    wheel while aiming with the sniper or the camera"
    Write-Host "  classic B     Space (use-or-reload, as the cartridge's B)"
    Write-Host "  presentation  F5 / pad BACK cycles ORIGINAL -> COMMUNITY HD -> XBLA"
    Write-Host "                (one selection: artwork AND external world assets)"
    Write-Host "  gamepad       unchanged: A/B use-or-reload, X next weapon,"
    Write-Host "                aim + C-down crouch, per the selected control style"
    Write-Host "  watch / pause Tab or Esc, pad Start"
    Write-Host "  d-pad         arrow keys"
    Write-Host "  menus         move the MOUSE to point, left click to select. W A S D"
    Write-Host "                or the arrows move, the wheel steps up and down, Enter"
    Write-Host "                selects, Esc backs out"
    Write-Host "  intros        left click, or any button, skips one screen"
    Write-Host "  quit          close the window (Alt+F4)"
    Write-Host ""
    Write-Host "THE FRONT END IS POINTED AT WITH THE MOUSE. GoldenEye's own menus"
    Write-Host "have always worked from a cursor - the mouse moves that cursor, so the"
    Write-Host "highlight you see is the game's, and left click is its A button. The"
    Write-Host "pointer is confined to the window there but stays VISIBLE."
    Write-Host ""
    Write-Host "IN PLAY the pointer is not taken until you click in the window. That"
    Write-Host "first click only captures - it does not fire, and neither does the"
    Write-Host "click that started the level. Opening the watch gives the pointer back"
    Write-Host "and closing it takes it again with no second click; alt-tabbing away"
    Write-Host "releases it until you come back. SL_MOUSE=0 disables all of it and"
    Write-Host "leaves the keyboard working."
    Write-Host ""
    Write-Host "Esc does NOT quit. Tab and Esc are the only keys that open the watch."
    Write-Host "Closing the window is the way out."
    Write-Host ""
    Write-Host "Keyboard and mouse feel the same whichever control style is selected."
    Write-Host "They do not go through the N64 stick for movement. The GAMEPAD is"
    Write-Host "unchanged and still follows the in-game control style; for modern"
    Write-Host "twin-stick on a pad pick 2.2 Galore."
    Write-Host ""
    if ($directBoot) {
        Write-Host "Press nothing at the start. The level opens on its own intro camera,"
        Write-Host "then hands you first person facing the airlock door."
    } else {
        Write-Host "This is the game's OWN front end: the boot sequence, then file"
        Write-Host "select, mission select and difficulty select. It is playable with"
        Write-Host "the MOUSE ALONE - click through the intros one screen per click,"
        Write-Host "then point and click your way to a level. Naming a level skips all"
        Write-Host "of it and boots straight in:"
    }
    Write-Host ""
    Write-Host "  .\tools\windows\play.ps1 -Level dam -Difficulty 2"
    Write-Host "  `$env:SL_MOUSE_SENS=10    faster look (default 6)"
    Write-Host "  INVERT MOUSE Y           the front end's OPTIONS > SETTINGS > CONTROL row, or"
    Write-Host "                           the watch's SIGHTLINE page; saved to"
    Write-Host "                           %LOCALAPPDATA%\sightline\config.ini (the controller's"
    Write-Host "                           Look Up/Down option in Bond's watch is the gamepad's)."
    Write-Host "                           `$env:SL_MOUSE_INVERT is a developer seed for runs"
    Write-Host "                           WITHOUT a config (replay, headless); a player launch"
    Write-Host "                           reports it on stderr and ignores it"
    Write-Host "  `$env:SL_MOUSE=0          never capture the pointer"
    Write-Host "  `$env:SL_LOOK_INVERT=1    invert GAMEPAD stick pitch (developer override; not the mouse)"
    Write-Host "  `$env:SL_FPS=30           pace to something other than 60"
    Write-Host ""
    Write-Host "  PRESENTATION             F5, the pad's BACK, OPTIONS > SETTINGS > DISPLAY,"
    Write-Host "                           or the watch's SIGHTLINE > GRAPHICS - all one"
    Write-Host "                           value: ORIGINAL / COMMUNITY HD / XBLA. It chooses"
    Write-Host "                           the artwork AND the external world assets together,"
    Write-Host "                           so XBLA brings the Dam's skydome with it."
    Write-Host "                           Texture packs: $texpackShown ($texpackWhy)"
    Write-Host "                           World assets: $waShown ($waWhy)"
    Write-Host "                           .\tools\windows\prepare-textures.ps1 and"
    Write-Host "                           .\tools\windows\prepare-xbla-assets.ps1 build them"
    Write-Host "                           from your own sources; ORIGINAL needs neither"
    Write-Host ""
}

# Only the launch settings are set here; everything else the player put in the
# environment is passed through untouched.
$saved = @{}
$vars = @{
    SL_ROM         = $rom
    SL_WINDOW      = '1'
    SL_WINDOW_SIZE = $Size
    SL_EEPROM_RW   = $save
}
# Established here with everything else, so there is ONE place the child's
# environment is set and nothing for a later run to reproduce by hand.
if ($texpack) { $vars['SL_TEXPACK_ROOT'] = $texpack }
# #68: the ROOT is handed over whenever this launcher found one, exactly as
# the texture pack root is, because the presentation - not a switch - decides
# whether anything is read from it. A root costs nothing when the presentation
# selects no world assets: no directory is opened.
if ($waRoot) { $vars['SL_WORLDASSET_ROOT'] = $waRoot }
# The SELECTION is normally not in the environment at all. -WorldAssets pins
# it for a diagnostic run; FOLLOW clears any stale value so the presentation
# is the only thing deciding.
if ($WorldAssets -eq 'XBLA') {
    $vars['SL_WORLD_ASSETS'] = 'xbla'
} elseif ($WorldAssets -eq 'OFF') {
    $vars['SL_WORLD_ASSETS'] = 'off'
}
# Only a NAMED level sets the direct-boot seam. Without it the binary leaves
# g_StageNum at LEVELID_TITLE (boss.c:112) and boots the way the cartridge does.
if ($directBoot) {
    $vars['SL_BOOT_LEVEL']      = $stage
    $vars['SL_BOOT_DIFFICULTY'] = $Difficulty
}

# -TeleportMark: DEVELOPER mark-teleport. This launcher does ALL of the F8
# mark parsing here and hands the binary already-split scalars
# (SL_TELEPORT_X/Y/Z/THETA/VERTA/ROOM); src/native/sl_teleport.c never parses a
# report. The mark - not the recorded input stream - is the navigation anchor:
# a recorded replay drifts from the live session (measured), so it cannot put
# the camera where the mark was taken. This does.
#
# The mark's authoritative viewpoint values live in a compact [teleport]
# section on marks recorded since this feature landed; a LEGACY mark predating
# it is still supported by falling back to the [camera] eye-world-pos and
# look-theta/verta plus the [rooms] player-room, which are the same
# quantities. Direct boot is required - there is nowhere to teleport in the
# front end.
if ($TeleportMark) {
    if (-not $directBoot) {
        Write-Host 'sightline: -TeleportMark needs a -Level to boot into.'
        exit 1
    }
    if (-not (Test-Path -LiteralPath $TeleportMark)) {
        Write-Host "sightline: no mark at '$TeleportMark'."
        exit 1
    }
    $markText = [IO.File]::ReadAllText((Resolve-Path -LiteralPath $TeleportMark).Path)

    $tpx = $null; $tpy = $null; $tpz = $null
    $tptheta = $null; $tpverta = $null; $tproom = $null

    # Prefer the explicit [teleport] section when the mark carries one.
    if ($markText -match '(?m)^teleport-pos\s+(-?\d+(?:\.\d+)?)\s+(-?\d+(?:\.\d+)?)\s+(-?\d+(?:\.\d+)?)') {
        $tpx = $Matches[1]; $tpy = $Matches[2]; $tpz = $Matches[3]
    }
    if ($markText -match '(?m)^teleport-theta\s+(-?\d+(?:\.\d+)?)') { $tptheta = $Matches[1] }
    if ($markText -match '(?m)^teleport-verta\s+(-?\d+(?:\.\d+)?)') { $tpverta = $Matches[1] }
    if ($markText -match '(?m)^teleport-room\s+(-?\d+)')            { $tproom  = $Matches[1] }

    # LEGACY fallback: derive the same quantities from [camera] and [rooms].
    if ($null -eq $tpx -and $markText -match '(?m)^eye-world-pos\s+(-?\d+(?:\.\d+)?)\s+(-?\d+(?:\.\d+)?)\s+(-?\d+(?:\.\d+)?)') {
        $tpx = $Matches[1]; $tpy = $Matches[2]; $tpz = $Matches[3]
    }
    if ($null -eq $tptheta -and $markText -match '(?m)^look-theta-deg\s+(-?\d+(?:\.\d+)?)') { $tptheta = $Matches[1] }
    if ($null -eq $tpverta -and $markText -match '(?m)^look-verta-deg\s+(-?\d+(?:\.\d+)?)') { $tpverta = $Matches[1] }
    if ($null -eq $tproom  -and $markText -match '(?m)^player-room\s+(-?\d+)')              { $tproom  = $Matches[1] }

    if ($null -eq $tpx -or $null -eq $tptheta -or $null -eq $tpverta) {
        Write-Host 'sightline: could not read a viewpoint from that mark.'
        Write-Host '  Expected a [teleport] section, or [camera] eye-world-pos'
        Write-Host '  with look-theta-deg / look-verta-deg.'
        exit 1
    }
    if ($null -eq $tproom) { $tproom = '-1' }

    $vars['SL_TELEPORT']       = '1'
    $vars['SL_TELEPORT_X']     = $tpx
    $vars['SL_TELEPORT_Y']     = $tpy
    $vars['SL_TELEPORT_Z']     = $tpz
    $vars['SL_TELEPORT_THETA'] = $tptheta
    $vars['SL_TELEPORT_VERTA'] = $tpverta
    $vars['SL_TELEPORT_ROOM']  = $tproom

    if (-not $Quiet) {
        Write-Host ''
        Write-Host "Teleport: pos=($tpx, $tpy, $tpz) theta=$tptheta verta=$tpverta room=$tproom"
        Write-Host "  from mark: $TeleportMark"
        Write-Host '  applied once after level/player init; no input replay.'
        Write-Host ''
    }
}

# -Cheats / -Weapon: DEVELOPER cheats and starting weapon, native side
# (src/native/sl_cheat.c). Same shape as the teleport: this launcher only
# hands the strings over, the binary resolves the names against the game's own
# CHEAT_IDS / ITEM_IDS tables and applies them through the game's own cheat
# and inventory paths. Direct boot only, for the same reason as the teleport.
if ($Cheats -or $Weapon) {
    if (-not $directBoot) {
        Write-Host 'sightline: -Cheats / -Weapon need a -Level to boot into.'
        exit 1
    }
    if ($Cheats) { $vars['SL_CHEATS'] = ($Cheats -join ',') }
    if ($Weapon) { $vars['SL_WEAPON'] = $Weapon }
    if (-not $Quiet) {
        Write-Host ''
        if ($Cheats) { Write-Host "Cheats: $($Cheats -join ',')  (the game's own cheat menu state, applied at level start)" }
        if ($Weapon) { Write-Host "Weapon: $Weapon  (right hand, once the level is in first person)" }
        Write-Host ''
    }
}

# REPLAY THROUGH THIS LAUNCHER ALREADY WORKS, and the claim that it does not is
# REFUTED - recorded here because it was believed, acted on, and is the kind of
# thing that will be believed again.
#
# The table above SETS four variables. It does not define the child's whole
# environment: the default path invokes the exe directly and every other SL_*
# in this shell is inherited untouched, which is what the comment above means.
# So `$env:SL_INPUT=...; .\tools\windows\play.ps1 -Level dam` IS a replay.
#
# MEASURED 2026-09-08, this file at HEAD against this file modified, same
# stream, same frame count: BOTH children printed
# `sightline native: input stream ...rt2.input (8326 reads)`. Adding SL_INPUT
# to the table changed nothing, and it would have COST something - see below.
#
# What was actually observed, and misread: no run directory produced through a
# replay has SL_INPUT in its `env` sidecar. That is because a replay produces
# NO RUN DIRECTORY AT ALL - sl_main.c returns early when SL_INPUT is set, on
# purpose, since the input stream such a run would record is a copy of the one
# it was handed. Absence of the run dir was read as absence of the variable.
#
# DO NOT "fix" this by adding SL_INPUT / SL_MOVE / SL_VIS / SL_RNG_SEED to the
# table. -MeasurementMode clears every SL_* the table does not name, and that
# is the one guarantee that mode exists to make; naming them here would exempt
# them from that sweep and quietly turn a measurement run into a replay.
# -MeasurementMode dropping a replay is correct behaviour, not a bug.
# The ONE key that differs, added to the SAME table so there is a single place
# the child's environment is established and the comparison stays controlled.
if ($NoConsoleCapture) { $vars['SL_RUN'] = '0' }

# THE MEASUREMENT LAUNCH. Same table, for the same reason: one place where the
# child's environment is established, so there is nothing for a caller to
# reproduce by hand and get subtly wrong.
$mmOut = $null
$mmErr = $null
$mmSave = $null
$mmCleared = @()
# #68: the DEFAULT path removes a stale SL_WORLD_ASSETS from the child's
# environment, through the same list -MeasurementMode uses, so the
# presentation is the only thing selecting a world-asset set. Announced
# further up; listed here so the restore at the end puts it back.
if ($waClearedStale) { $mmCleared += 'SL_WORLD_ASSETS' }
if ($MeasurementMode) {
    # Derived at RUNTIME, never a literal: this repository is cloned by other
    # people onto other drives under other user names. Same ladder the $save
    # block above uses, with SL_MEASURE_DIR as the override.
    $mmDir = $env:SL_MEASURE_DIR
    if (-not $mmDir) {
        $mmBase = $env:LOCALAPPDATA
        if (-not $mmBase -and $env:USERPROFILE) { $mmBase = Join-Path $env:USERPROFILE 'AppData\Local' }
        if (-not $mmBase) { $mmBase = $env:TEMP }
        if (-not $mmBase) {
            Write-Host 'sightline: -MeasurementMode has nowhere to put the scratch save and streams.'
            Write-Host '  None of LOCALAPPDATA, USERPROFILE or TEMP is set. Point'
            Write-Host '  SL_MEASURE_DIR at a writable directory and try again.'
            exit 1
        }
        $mmDir = Join-Path $mmBase 'sightline\measurement'
    }
    New-Item -ItemType Directory -Force -Path $mmDir | Out-Null
    $mmStamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $mmOut = Join-Path $mmDir "$mmStamp.out.txt"
    $mmErr = Join-Path $mmDir "$mmStamp.err.txt"

    # A SCRATCH cartridge save, not the player's. Two reasons, both about
    # parity rather than safety: automated and manual runs boot from the same
    # stored state, and a measurement run never writes the save someone plays.
    $mmSave = Join-Path $mmDir 'eeprom.bin'

    $vars['SL_EEPROM_RW'] = $mmSave
    $vars['SL_RUN']       = '0'   # non-tty already skips capture; said out loud
    $vars['SL_FRAMES']    = '1600'

    # INSPECT, then clear. Everything the table above establishes is left
    # alone; every OTHER SL_* variable in this shell is removed for the child
    # and named in the manifest. SL_SHOT is the one that has to go by name -
    # the reconstructed launch removed it explicitly - and it is covered by
    # this same sweep rather than by a special case.
    foreach ($e in Get-ChildItem env: | Where-Object { $_.Name -like 'SL_*' }) {
        if (-not $vars.ContainsKey($e.Name) -and $mmCleared -notcontains $e.Name) { $mmCleared += $e.Name }
    }
    $mmCleared = @($mmCleared | Sort-Object)
}

# Where the redirected streams go: outside the repository, and SAID OUT LOUD
# while the console can still print, which is the whole reason this is here
# rather than three lines further down.
$ncOut = $null
$ncErr = $null
if ($NoConsoleCapture) {
    # Derived at RUNTIME, never a literal: this repository is cloned by other
    # people onto other drives under other user names. Same ladder the $save
    # block above uses, with SL_NOCAPTURE_DIR as the override.
    $ncDir = $env:SL_NOCAPTURE_DIR
    if (-not $ncDir) {
        $ncBase = $env:LOCALAPPDATA
        if (-not $ncBase -and $env:USERPROFILE) { $ncBase = Join-Path $env:USERPROFILE 'AppData\Local' }
        if (-not $ncBase) { $ncBase = $env:TEMP }
        if (-not $ncBase) {
            Write-Host 'sightline: -NoConsoleCapture has nowhere to put the redirected streams.'
            Write-Host '  None of LOCALAPPDATA, USERPROFILE or TEMP is set. Point'
            Write-Host '  SL_NOCAPTURE_DIR at a writable directory and try again.'
            exit 1
        }
        $ncDir = Join-Path $ncBase 'sightline\nocapture'
    }
    New-Item -ItemType Directory -Force -Path $ncDir | Out-Null
    $ncStamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $ncOut = Join-Path $ncDir "$ncStamp.out.txt"
    $ncErr = Join-Path $ncDir "$ncStamp.err.txt"
    Write-Host ""
    Write-Host "-NoConsoleCapture: run capture is OFF and this run is NOT recorded."
    Write-Host "  stdout -> $ncOut"
    Write-Host "  stderr -> $ncErr"
    Write-Host "  The game window opens as usual; only the streams moved."
    Write-Host ""
}
# THE MANIFEST. Printed before anything launches so a manual run and an automated one have
# the same short list of concrete values to compare, rather than two prose
# descriptions of what each believes it ran. Knobs only - this is not a dump of
# the environment.
if ($MeasurementMode) {
    if ($null -eq $env:SL_SHOT) { $mmShot = 'not set' }
    else { $mmShot = "REMOVED for the child (was '$env:SL_SHOT')" }
    Write-Host ""
    Write-Host "-MeasurementMode: the canonical measurement launch. NOT recorded."
    Write-Host "  exe            $exe"
    Write-Host "  cwd            $repo"
    Write-Host "  window         SL_WINDOW=1, $Size"
    Write-Host "  boot           $(if ($directBoot) { "level $Level (stage $stage), difficulty $Difficulty" } else { 'front end (no SL_BOOT_LEVEL)' })"
    Write-Host "  eeprom         $mmSave (scratch)"
    Write-Host "  texpack root   $texpackShown ($texpackWhy)"
    Write-Host "  presentation   $presName ($presWhy)"
    Write-Host "  world assets   $(if ($WorldAssets -eq 'FOLLOW') { "$waDerived - from the presentation" } else { "$WorldAssets - PINNED by -WorldAssets" }) - root $waShown ($waWhy)"
    Write-Host "  stdout         $mmOut"
    Write-Host "  stderr         $mmErr"
    Write-Host "  console        Start-Process WITHOUT -NoNewWindow (child gets its own console - expected)"
    Write-Host "  SL_RUN         0"
    Write-Host "  SL_FRAMES      1600 (run exits at frame 1600, ~27 s; Nintendo is near 8.3 s)"
    Write-Host "  SL_SHOT        $mmShot"
    if ($mmCleared.Count -gt 0) {
        Write-Host "  cleared        $($mmCleared -join ', ')"
    } else {
        Write-Host "  cleared        nothing - no other SL_* was set"
    }
    Write-Host ""
}

$savedCwd = (Get-Location).Path
try {
    foreach ($k in $vars.Keys) {
        $saved[$k] = [System.Environment]::GetEnvironmentVariable($k)
        Set-Item -Path "env:$k" -Value $vars[$k]
    }
    # Saved the same way the table is, so the finally block restores both
    # without knowing which was which.
    foreach ($k in $mmCleared) {
        $saved[$k] = [System.Environment]::GetEnvironmentVariable($k)
        Remove-Item -Path "env:$k" -ErrorAction SilentlyContinue
    }
    Set-Location $repo
    if ($MeasurementMode) {
        # THE PROCESS CREATION IS THE POINT. Start-Process with the streams
        # redirected, -PassThru and -Wait, and NO -NoNewWindow: that is what
        # the known-good run did, so that is what this does. The child is
        # created with a console of its own and an empty black window appears
        # beside the game. DO NOT ADD -NoNewWindow HERE to tidy that up. The
        # flag below has it because it is a different mode with a different
        # purpose; adding it here would recreate the exact divergence - two
        # launches described as one - that this mode was written to end.
        $spArgs = @{ FilePath = $exe; WorkingDirectory = $repo; Wait = $true; PassThru = $true
                     RedirectStandardOutput = $mmOut; RedirectStandardError = $mmErr }
        if ($args.Count -gt 0) { $spArgs['ArgumentList'] = $args }
        $proc = Start-Process @spArgs
        $code = $proc.ExitCode
        Write-Host ""
        foreach ($mm in @(@('stdout', $mmOut), @('stderr', $mmErr))) {
            if (Test-Path -LiteralPath $mm[1]) {
                Write-Host ("{0} {1} bytes -> {2}" -f $mm[0], (Get-Item -LiteralPath $mm[1]).Length, $mm[1])
            } else {
                Write-Host ("{0} -> {1} (not written)" -f $mm[0], $mm[1])
            }
        }
    } elseif ($NoConsoleCapture) {
        # Start-Process, not a shell redirect: `>` and `2>` on a native exe
        # have produced silently empty files here, and -PassThru without -Wait
        # never populates ExitCode. The window is SDL's own and is unaffected
        # by where the streams point.
        #
        # -NoNewWindow is LOAD-BEARING, not cosmetic. Measured: without it
        # Start-Process passes CREATE_NEW_CONSOLE, and the child came up owning
        # a console of its own (GetConsoleWindow non-zero, process list of 1) -
        # an empty black window alongside the game for the whole session. With
        # it the child takes this console if there is one and none if there is
        # not. That is what THIS mode wants - a playable launch with no second
        # window - and it is NOT what -MeasurementMode does; the measurement
        # launch being reproduced had no -NoNewWindow at all.
        $spArgs = @{ FilePath = $exe; WorkingDirectory = $repo; Wait = $true; PassThru = $true
                     NoNewWindow = $true
                     RedirectStandardOutput = $ncOut; RedirectStandardError = $ncErr }
        if ($args.Count -gt 0) { $spArgs['ArgumentList'] = $args }
        $proc = Start-Process @spArgs
        $code = $proc.ExitCode
        Write-Host ""
        # Report what is THERE, not what was asked for: if the child never
        # started, or started and was killed before .NET flushed the handle,
        # the file can be absent and Get-Item would throw under
        # $ErrorActionPreference = 'Stop', losing the exit code on the way out.
        foreach ($nc in @(@('stdout', $ncOut), @('stderr', $ncErr))) {
            if (Test-Path -LiteralPath $nc[1]) {
                Write-Host ("{0} {1} bytes -> {2}" -f $nc[0], (Get-Item -LiteralPath $nc[1]).Length, $nc[1])
            } else {
                Write-Host ("{0} -> {1} (not written)" -f $nc[0], $nc[1])
            }
        }
    } else {
        # Straight through, console attached: the build is -mconsole precisely so
        # its stderr diagnostics land somewhere you can read them.
        & $exe @args
        $code = $LASTEXITCODE
    }
}
finally {
    foreach ($k in $saved.Keys) {
        if ($null -eq $saved[$k]) { Remove-Item -Path "env:$k" -ErrorAction SilentlyContinue }
        else { Set-Item -Path "env:$k" -Value $saved[$k] }
    }
    Set-Location $savedCwd
}
exit $code
