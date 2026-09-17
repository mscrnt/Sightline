<#
.SYNOPSIS
    Build ONE self-contained SightlineDemo.exe.

.DESCRIPTION
    Run from an ordinary Windows PowerShell prompt:

        .\tools\windows\package-demo.ps1 -Rom <path to your ROM> `
                                         -OutFile <path>\SightlineDemo.exe

    THE REPOSITORY CONTAINS NO ROM. This tool consumes a ROM the user
    supplies with -Rom; nothing it packages is tracked here.

    The output is a single file the user can drop in an empty folder and
    double-click. At runtime it needs no repository, no PowerShell, no
    play.ps1, no MSYS2, no Python, no environment variables, no ROM selection,
    no DLL copying and no asset install step: everything is carried inside it
    as RCDATA and unpacked into a private hash-addressed cache under
    %LOCALAPPDATA%\sightline\demo\runtime\<payload id>.

    THE OUTPUT IS PRIVATE AND LOCAL. It contains the user's own ROM and the
    assets derived from it, so it is not redistributable and no claim to the
    contrary is made anywhere. Project rule 2 keeps every one of those bytes
    out of git; so does .gitignore (build\, *.z64 and SightlineDemo.exe are
    all ignored), and the two generated intermediates this script writes
    live under build\ for exactly that reason.

    WHAT GOES IN, AND HOW EACH PART IS FOUND

      the demo core    build\win32\sightline.exe, from build.ps1 -Demo. Built
                       here unless -SkipBuild, and the -Demo identity is not
                       optional: it is what makes the demo policy (the full
                       single-player campaign, every menu cheat unlocked and
                       none active, no Multiplayer, its own isolated save) a
                       property of the binary rather than of how it was
                       launched.

      the DLLs         MEASURED from the PE import table with objdump, not
                       listed. An imported name is non-system when a file of
                       that name sits beside the core or in the mingw32 bin
                       directory; the search is transitive, so a DLL pulled in
                       by another DLL is found too.

      the ROM          -Rom, the only required argument. Never searched for,
                       never guessed, and never read from SL_ROM: the point of
                       this build is that the finished demo has exactly one
                       ROM and it is the one named here.

      the assets       build\win32\data\asset-overrides, which is where
                       build.ps1 puts the built .slmodel overrides. Whatever
                       is in that tree is what ships - the set is read off
                       disk rather than named, so an asset added later needs
                       no edit here.

    WHAT COMES OUT
        A manifest is printed - every item with its size and SHA-256, plus the
        aggregate payload id and the output path - and only then is the
        executable assembled. The destination is replaced at the very end,
        after the link has succeeded, so a failed run never leaves a
        half-written demo where a working one used to be.

.PARAMETER Rom
    The GoldenEye (U) ROM to embed. Required.

.PARAMETER OutFile
    Where to write SightlineDemo.exe. Required.

.PARAMETER SkipBuild
    Package build\win32\sightline.exe as it stands instead of rebuilding.
    Only correct when that exe is already a fresh -Demo build; this script
    cannot tell a demo build from a normal one by looking at it, which is why
    rebuilding is the default.

.PARAMETER Msys2Root
    Where MSYS2 lives, if it is not in one of the usual places. SL_MSYS2_ROOT
    does the same thing. Same probe as build.ps1: nothing here is a hardcoded
    machine path.

.NOTES
    The bootstrap source is tools\windows\sl_demo_bootstrap.c and is generic -
    no payload, no paths, no hashes. Everything payload-specific reaches it
    through the generated manifest resource.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Rom,
    [Parameter(Mandatory = $true)][string]$OutFile,
    [switch]$SkipBuild,
    [string]$Msys2Root
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$OUT  = Join-Path $repo 'build\win32'

# ---------------------------------------------------------------- helpers --

function Find-Mingw32Bin {
    param([string]$Override)
    # Probe, never hardcode - the same ladder build.ps1 walks, for the same
    # reason: a repository file must not carry one developer's install path.
    $explicit = $Override
    if (-not $explicit) { $explicit = $env:SL_MSYS2_ROOT }
    $roots = New-Object System.Collections.Generic.List[string]
    if ($explicit) {
        $roots.Add($explicit)
    } else {
        foreach ($r in @("$env:SystemDrive\msys64", 'C:\msys64', 'C:\msys32',
                         "$env:ProgramFiles\msys64",
                         "$env:LOCALAPPDATA\Programs\msys64")) {
            if ($r) { $roots.Add($r) }
        }
    }
    foreach ($r in $roots) {
        $bin = Join-Path $r 'mingw32\bin'
        if (Test-Path (Join-Path $bin 'i686-w64-mingw32-gcc.exe')) { return $bin }
    }
    if ($explicit) { return $null }
    $onPath = Get-Command 'i686-w64-mingw32-gcc.exe' -ErrorAction SilentlyContinue
    if ($null -ne $onPath) { return (Split-Path -Parent $onPath.Source) }
    return $null
}

function Invoke-Tool {
    param([string]$Exe, [string[]]$Arguments, [string]$What)
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$What failed (exit $LASTEXITCODE)`n  $Exe $($Arguments -join ' ')"
    }
}

function Get-Sha256 {
    param([string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Write-TextFileLf {
    param([string]$Path, [string]$Text)
    # LF and no BOM. The bootstrap's parser tolerates CRLF, but the manifest
    # is hashed into nothing and read as bytes; one encoding is one fewer
    # thing that can differ between machines.
    [System.IO.File]::WriteAllText($Path, $Text, (New-Object System.Text.UTF8Encoding($false)))
}

# The PE import names of one image, read with objdump rather than guessed.
function Get-PeImports {
    param([string]$Objdump, [string]$Image)
    $out = & $Objdump -p $Image
    if ($LASTEXITCODE -ne 0) { throw "objdump -p failed on $Image" }
    return @($out | Select-String 'DLL Name:\s*(\S+)' |
             ForEach-Object { $_.Matches[0].Groups[1].Value })
}

# ------------------------------------------------------------- toolchain ---

$mingwBin = Find-Mingw32Bin -Override $Msys2Root
if ($null -eq $mingwBin) {
    Write-Error ("package-demo.ps1: no i686-w64-mingw32-gcc.exe found.`n" +
                 "  Run .\tools\windows\setup.ps1, or set SL_MSYS2_ROOT.")
    exit 1
}
$CC      = Join-Path $mingwBin 'i686-w64-mingw32-gcc.exe'
$WINDRES = Join-Path $mingwBin 'windres.exe'
$OBJDUMP = Join-Path $mingwBin 'objdump.exe'
foreach ($t in @($CC, $WINDRES, $OBJDUMP)) {
    if (-not (Test-Path $t)) {
        Write-Error "package-demo.ps1: missing $t. Run .\tools\windows\setup.ps1."
        exit 1
    }
}

# ----------------------------------------------------------------- inputs --

if (-not (Test-Path -LiteralPath $Rom)) {
    Write-Error ("package-demo.ps1: no ROM at '$Rom'.`n" +
                 "  -Rom names YOUR OWN GoldenEye (U) copy. Nothing ROM-derived`n" +
                 "  ships in this repository and nothing is downloaded.")
    exit 1
}
$Rom = (Resolve-Path -LiteralPath $Rom).Path

# Resolved before anything is built, so a bad -OutFile fails in a second
# rather than after a full compile.
$outDir = Split-Path -Parent $OutFile
if (-not $outDir) { $outDir = (Get-Location).Path }
if (-not (Test-Path -LiteralPath $outDir)) {
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
}
$OutFile = Join-Path (Resolve-Path -LiteralPath $outDir).Path (Split-Path -Leaf $OutFile)

$core = Join-Path $OUT 'sightline.exe'
if (-not $SkipBuild) {
    Write-Host ''
    Write-Host 'package-demo: building the demo core (build.ps1 -Demo)'
    & (Join-Path $PSScriptRoot 'build.ps1') -Demo
    if ($LASTEXITCODE -ne 0) {
        Write-Error 'package-demo.ps1: the demo build failed; nothing packaged.'
        exit 1
    }
}
if (-not (Test-Path -LiteralPath $core)) {
    Write-Error "package-demo.ps1: no core at $core."
    exit 1
}

# The demo build is not a compile-time claim this script can make on its own,
# so it says out loud which key the objects were built under. .build_key is
# written by build.ps1 and carries the -D list verbatim.
$keyFile = Join-Path $OUT '.build_key'
$isDemo = $false
if (Test-Path -LiteralPath $keyFile) {
    $isDemo = ([System.IO.File]::ReadAllText($keyFile)).Contains('-DSL_DEMO_BUILD')
}
if (-not $isDemo) {
    Write-Error ("package-demo.ps1: build\win32 was not built with -Demo.`n" +
                 "  build\win32\.build_key does not contain -DSL_DEMO_BUILD, so`n" +
                 "  this core carries no demo policy. Re-run without -SkipBuild.")
    exit 1
}

# ------------------------------------------------------- the payload list --

# Every entry: LogicalName, Source (on this machine), Rel (inside the cache).
$items = New-Object System.Collections.Generic.List[object]

function Add-Item2 {
    param([string]$Logical, [string]$Source, [string]$Rel)
    $items.Add([pscustomobject]@{
        Logical = $Logical
        Source  = (Resolve-Path -LiteralPath $Source).Path
        Rel     = $Rel
    })
}

# THE CORE, and the DLLs BESIDE IT. Not in a bin\ of their own: the Windows
# loader searches the directory of the executable image first, and putting the
# imports anywhere else would need a PATH or a SetDllDirectory call that this
# has no reason to invent.
Add-Item2 'demo core' $core 'core\sightline.exe'

# Transitive PE import walk. A name counts as non-system when a file of that
# name exists beside the core (build.ps1 already staged the direct imports
# there) or in the mingw32 bin directory. Everything else - KERNEL32, USER32,
# OPENGL32, msvcrt - is Windows' own and must NOT be shipped.
$seenDll  = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
$queue    = New-Object System.Collections.Generic.Queue[string]
$dllPaths = New-Object System.Collections.Generic.List[string]
foreach ($n in (Get-PeImports $OBJDUMP $core)) { $queue.Enqueue($n) }
while ($queue.Count -gt 0) {
    $name = $queue.Dequeue()
    if ($seenDll.Contains($name)) { continue }
    [void]$seenDll.Add($name)
    $src = $null
    foreach ($cand in @((Join-Path $OUT $name), (Join-Path $mingwBin $name))) {
        if (Test-Path -LiteralPath $cand) { $src = $cand; break }
    }
    if ($null -eq $src) { continue }   # a system DLL; Windows supplies it
    $dllPaths.Add($src)
    foreach ($n2 in (Get-PeImports $OBJDUMP $src)) { $queue.Enqueue($n2) }
}
foreach ($d in ($dllPaths | Sort-Object)) {
    Add-Item2 'dll' $d ('core\' + (Split-Path -Leaf $d))
}

# THE ROM.
Add-Item2 'rom' $Rom 'rom\baserom.z64'

# THE ASSETS. Read off disk, not named: build.ps1 builds every committed
# override source into build\win32\data\asset-overrides, and whatever is there
# is what the running game loads. The relative shape under that directory is
# the shape sl_asset_override.c expects (boot\<id>.slmodel), so it is carried
# across unchanged and SL_ASSET_OVERRIDE_DIR is pointed at the root of it.
$assetSrc = Join-Path $OUT 'data\asset-overrides'
$assetN = 0
if (Test-Path -LiteralPath $assetSrc) {
    $prefix = (Resolve-Path -LiteralPath $assetSrc).Path
    foreach ($f in (Get-ChildItem -LiteralPath $assetSrc -Recurse -File | Sort-Object FullName)) {
        $rel = $f.FullName.Substring($prefix.Length).TrimStart('\')
        Add-Item2 'asset' $f.FullName ('assets\' + $rel)
        $assetN++
    }
}
if ($assetN -eq 0) {
    Write-Error ("package-demo.ps1: no built assets in $assetSrc.`n" +
                 "  build.ps1 writes the .slmodel overrides there. Without them the`n" +
                 "  demo would show the ORIGINAL logos, which is not the showcase.")
    exit 1
}

# A space in any of these would break the manifest's one-space field split,
# and the bootstrap would read a truncated path. Refuse loudly here rather
# than ship something that fails on the owner's machine.
foreach ($it in $items) {
    if ($it.Rel -match '\s') { throw "package-demo.ps1: '$($it.Rel)' contains whitespace." }
}

# ------------------------------------------------------ hash and identify --

$i = 0
foreach ($it in $items) {
    $it | Add-Member -NotePropertyName Size   -NotePropertyValue (Get-Item -LiteralPath $it.Source).Length
    $it | Add-Member -NotePropertyName Sha    -NotePropertyValue (Get-Sha256 $it.Source)
    $it | Add-Member -NotePropertyName ResName -NotePropertyValue ("PAYLOAD_$i")
    $i++
}

# THE PAYLOAD ID: a SHA-256 over the canonical listing of everything packaged.
# Any changed byte in any file, any renamed destination, any added or removed
# entry produces a different id, therefore a different cache directory, so an
# old extraction can never be mistaken for a new one.
$canon = ($items | Sort-Object Rel |
          ForEach-Object { "{0}`n{1}`n{2}`n" -f $_.Rel, $_.Size, $_.Sha }) -join ''
$sha = [System.Security.Cryptography.SHA256]::Create()
try {
    $idFull = ($sha.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($canon)) |
               ForEach-Object { $_.ToString('x2') }) -join ''
} finally { $sha.Dispose() }
# 128 bits of it names the directory. The full digest is in the manifest.
$payloadId = $idFull.Substring(0, 32)

# ------------------------------------------------------------ the manifest --

# THE RUNTIME ENVIRONMENT, and the whole of it. Each line below is here
# because tools\windows\play.ps1 establishes it on an ordinary playable launch
# and the runtime reads it; nothing is set speculatively.
#
#   SL_ROM                 sl_ultra_shim.c:1142, the ONE site that reads it.
#                          Absolute and inside the cache, so an ambient SL_ROM
#                          - which the bootstrap clears anyway - cannot decide
#                          which ROM the demo runs.
#   SL_ASSET_OVERRIDE_DIR  sl_asset_override.c, candidate 0 of the override
#                          ladder. Pointing it at the extracted assets is what
#                          stops the demo depending on, or being changed by,
#                          %LOCALAPPDATA%\sightline\assets - the directory the
#                          owner's own imports live in.
#   SL_WINDOW / _SIZE      a window, at play.ps1's default size.
#
# DELIBERATELY ABSENT, each for a measured reason:
#   SL_EEPROM / SL_EEPROM_RW   the -Demo build OVERRIDES both and forces
#                              %LOCALAPPDATA%\sightline\demo\eeprom.bin
#                              (sl_ultra_shim.c:2236). Setting one here would
#                              be a second save policy competing with the
#                              build's, and the build would win anyway.
#   SL_RUN                     unset is what an ordinary play.ps1 launch does,
#                              so run capture behaves identically.
#   SL_FRAMES, SL_BOOT_LEVEL, SL_SHOT, SL_INPUT, SL_TRACE_OUT, and every other
#   developer knob            this is ordinary play. The bootstrap clears the
#                             whole SL_* namespace before setting the four
#                             above, so none of them can arrive from a shell.
$lines = New-Object System.Collections.Generic.List[string]
$lines.Add('# Sightline demo payload manifest - generated by tools/windows/package-demo.ps1')
$lines.Add("# aggregate $idFull")
$lines.Add("id $payloadId")
foreach ($it in $items) {
    # Parenthesised: inside a method call the commas would otherwise split the
    # -f argument list into four separate arguments to Add().
    $lines.Add(("file {0} {1} {2} {3}" -f $it.ResName, $it.Rel, $it.Size, $it.Sha))
}
$lines.Add('launch core\sightline.exe')
$lines.Add('cwd core')
$lines.Add('env SL_ROM {ROOT}\rom\baserom.z64')
$lines.Add('env SL_ASSET_OVERRIDE_DIR {ROOT}\assets')
$lines.Add('env SL_WINDOW 1')
$lines.Add('env SL_WINDOW_SIZE 960x720')

# ------------------------------------------------------------- print first --

Write-Host ''
Write-Host 'package-demo: payload'
Write-Host ''
foreach ($it in $items) {
    Write-Host ("  {0,-10} {1,-34} {2,10}  {3}" -f $it.Logical, $it.Rel, $it.Size, $it.Sha)
    Write-Host ("  {0,-10} {1}" -f '', $it.Source)
}
Write-Host ''
Write-Host ("  payload id   $payloadId   (sha256 $idFull)")
Write-Host ("  runtime      %LOCALAPPDATA%\sightline\demo\runtime\$payloadId")
Write-Host ("  output       $OutFile")
Write-Host ''

# ----------------------------------------------------------------- assemble --

# GENERATED, NEVER TRACKED. The .rc names absolute paths on this machine and
# the .o contains the ROM; both live under build\, which .gitignore covers.
$work = Join-Path $OUT 'tmp\demo-package'
if (Test-Path -LiteralPath $work) { Remove-Item -Recurse -Force -LiteralPath $work }
New-Item -ItemType Directory -Force -Path $work | Out-Null

$manPath = Join-Path $work 'manifest.txt'
Write-TextFileLf $manPath (($lines -join "`n") + "`n")

# windres reads the .rc with C-string escaping, so a backslash in a path would
# start an escape sequence. Forward slashes are accepted on Windows by every
# API that opens these files.
$rc = New-Object System.Collections.Generic.List[string]
$rc.Add('/* generated by tools/windows/package-demo.ps1 - NEVER COMMIT THIS FILE. */')
$rc.Add(('SL_MANIFEST RCDATA "{0}"' -f ($manPath -replace '\\', '/')))
foreach ($it in $items) {
    $rc.Add(('{0} RCDATA "{1}"' -f $it.ResName, ($it.Source -replace '\\', '/')))
}
$rcPath = Join-Path $work 'payload.rc'
Write-TextFileLf $rcPath (($rc -join "`n") + "`n")

$resObj = Join-Path $work 'payload.o'
$tmpExe = Join-Path $work 'SightlineDemo.exe'

# PATH and TMP for this script's child processes only, restored in the
# finally. Both are carried over from build.ps1 rather than rediscovered:
# mingw32\bin has to be on PATH for the driver to find its own components,
# and gcc stages temporaries through TMP - with it unset it has been measured
# choosing a directory it could not write and failing with a message that
# names neither the cause nor the file.
$savedPath = $env:PATH
$savedTmp  = $env:TMP
$savedTemp = $env:TEMP
$savedTmpd = $env:TMPDIR
try {
    $env:PATH   = "$mingwBin;$env:PATH"
    $env:TMP    = $work
    $env:TEMP   = $work
    $env:TMPDIR = $work

    Write-Host 'package-demo: compiling resources'
    Invoke-Tool $WINDRES @('-F', 'pe-i386', '-i', $rcPath, '-o', $resObj) 'windres'

    Write-Host 'package-demo: linking bootstrap'
    # -mconsole is LOAD-BEARING, not a default left in place. The core branches
    # on isatty (src/platform/sl_main.c:213, :294) and the accepted ordinary
    # launch gives it a terminal; a -mwindows bootstrap would hand it none and
    # silently change which branches run. The bootstrap hides that console
    # window when it owns it, which keeps the console ATTACHED - see the note
    # at the top of sl_demo_bootstrap.c.
    Invoke-Tool $CC @('-m32', '-O2', '-mconsole',
                      '-o', $tmpExe,
                      (Join-Path $PSScriptRoot 'sl_demo_bootstrap.c'), $resObj,
                      '-lbcrypt', '-luser32') 'link SightlineDemo.exe'
}
finally {
    $env:PATH   = $savedPath
    $env:TMP    = $savedTmp
    $env:TEMP   = $savedTemp
    $env:TMPDIR = $savedTmpd
}

if (-not (Test-Path -LiteralPath $tmpExe)) {
    throw 'package-demo.ps1: the link reported success but produced no file.'
}

# REPLACE ONLY NOW. Everything above can fail; the destination is untouched
# until there is a finished executable to put there.
Copy-Item -Force -LiteralPath $tmpExe -Destination $OutFile

$fi = Get-Item -LiteralPath $OutFile
Write-Host ''
Write-Host 'package-demo: done'
Write-Host ("  {0}" -f $fi.FullName)
Write-Host ("  {0} bytes" -f $fi.Length)
Write-Host ("  sha256 {0}" -f (Get-Sha256 $OutFile))
Write-Host ''
Write-Host '  PRIVATE AND LOCAL. This file contains your ROM and assets derived'
Write-Host '  from it. Keep it to yourself; it is not redistributable.'
Write-Host ''
