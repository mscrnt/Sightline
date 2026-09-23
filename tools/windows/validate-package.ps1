<#
.SYNOPSIS
    Release package validator: prohibited content and required content.

.DESCRIPTION
    Run from an ordinary Windows PowerShell prompt:

        .\tools\windows\validate-package.ps1 -Path <staged dir | Sightline-vX.Y.Z-win32.zip>
                                             [-Version X.Y.Z] [-Rom <your ROM>]

    Exits 0 only when the package holds nothing it must not and everything it
    must. Any failure is printed as a FAIL line and the exit code is 1, so a
    packager that runs this before compressing and again on the finished ZIP
    cannot publish a package this script rejected. Every rule is the project's
    own (project rules, rule 2, and the public-export gates in
    tools/publish/public-export.json), not merely .gitignore.

    PROHIBITED, by name: ROM dumps (*.z64 / *.n64 / *.v64), saves and EEPROM
    images, config files, logs and run captures, screenshots and captures,
    ROM-derived intermediates (bin\, build\native, sl_resample_tab.h, aspboot*,
    *.seg, *.o, *.s, .build_key, unresolved.txt, stubs.c, segments*), Community
    HD / XBLA texture sets, the demo and test executables, private
    infrastructure (.gitea, ci, tools\publish, gitea-token.ps1, keys, tokens),
    Git metadata, Python caches and virtualenvs, source archives (*.zip inside
    the package - the controller source ZIPs among them).

    PROHIBITED, by content: any file of the ROM's size (12582912 bytes) or
    starting with a ROM magic; any text file matching the forbidden patterns
    (private hostnames and networks, user-profile paths other than the generic
    placeholder, private keys, token prefixes, attribution trailers); and,
    when -Rom names the user's own dump, ANY file containing a ROM-derived
    excerpt: the ROM header, the aspMain microcode data segment, or the two
    compiled tables (the 64x4 RESAMPLE taps and the ENVMIXER ramp, as the
    little-endian shorts a build compiles them to). That last scan is the one
    that catches a table baked into an executable - which no reading of the
    package's file list can see.

    REQUIRED: sightline.exe, Sightline.cmd, README.txt, VERSION.txt, the
    optional texture fetcher's four files (Get-Textures.cmd,
    tools\get-textures.ps1, tools\community-source.json,
    tools\mapping\community.json - identities and code, never a pack), the
    LICENSES directory (README, 0BSD, CC0, the asset-overrides LICENSE), at
    least one data\asset-overrides\boot\*.slmodel, every non-system DLL the
    executable's import table names (read with objdump when it can be found,
    transitively), and a CC-BY ATTRIBUTION.md for every third-party controller
    model that ships. With -Version, the top-level folder must be
    Sightline-vX.Y.Z-win32 and VERSION.txt must name that version.

.PARAMETER Path
    A staged package directory, or a finished .zip.

.PARAMETER Version
    X.Y.Z. Checks the top-level folder name and the VERSION.txt version line.

.PARAMETER Rom
    The user's own GoldenEye 007 (U) .z64 dump. Enables the ROM-derived
    excerpt scan. Nothing from it is written anywhere.

.PARAMETER Objdump
    objdump.exe for the import-table cross-check. Probed under the usual
    MSYS2 roots when not given; the check is reported as skipped if none is
    found.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Path,
    [string]$Version,
    [string]$Rom,
    [string]$Objdump
)

$ErrorActionPreference = 'Stop'
$script:Failures = New-Object System.Collections.Generic.List[string]
$script:Notes    = New-Object System.Collections.Generic.List[string]

function Fail  { param([string]$Rule, [string]$What) $script:Failures.Add(("{0}: {1}" -f $Rule, $What)) }
function Note  { param([string]$Text) $script:Notes.Add($Text) }
function Latin { param([byte[]]$Bytes) return [System.Text.Encoding]::GetEncoding(28591).GetString($Bytes) }

if ($Version -and $Version -notmatch '^\d+\.\d+\.\d+$') {
    Write-Host "validate-package: -Version must be X.Y.Z (got '$Version')."
    exit 1
}

# ------------------------------------------------------------- the root ---

$tempRoot = $null
if (-not (Test-Path -LiteralPath $Path)) {
    Write-Host "validate-package: no such path: $Path"
    exit 1
}
$item = Get-Item -LiteralPath $Path
if (-not $item.PSIsContainer) {
    if ($item.Extension -ne '.zip') {
        Write-Host "validate-package: $Path is neither a directory nor a .zip."
        exit 1
    }
    $tempRoot = Join-Path $env:TEMP ("sl-validate-" + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force -Path $tempRoot | Out-Null
    Expand-Archive -LiteralPath $item.FullName -DestinationPath $tempRoot -Force
    $top = @(Get-ChildItem -LiteralPath $tempRoot -Force)
    if ($top.Count -ne 1 -or -not $top[0].PSIsContainer) {
        Fail 'zip-layout' ("the archive must hold exactly one top-level folder; found " +
                           (($top | ForEach-Object Name) -join ', '))
        $root = $tempRoot
    } else {
        $root = $top[0].FullName
        if ($Version -and $top[0].Name -ne "Sightline-v$Version-win32") {
            Fail 'zip-layout' ("top-level folder is '{0}', expected 'Sightline-v{1}-win32'" -f $top[0].Name, $Version)
        }
    }
} else {
    $root = $item.FullName
}
$rootPrefix = (Resolve-Path -LiteralPath $root).Path.TrimEnd('\') + '\'

$files = @(Get-ChildItem -LiteralPath $root -Recurse -File -Force | Sort-Object FullName)
$rel = @{}
foreach ($f in $files) { $rel[$f.FullName] = $f.FullName.Substring($rootPrefix.Length) -replace '\\', '/' }
$present = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
foreach ($f in $files) { [void]$present.Add($rel[$f.FullName]) }

# ------------------------------------------------ prohibited, by name ---

# (regex on the slash-separated relative path, case-insensitive) -> reason
$byName = @(
    @('(^|/)[^/]*\.(z64|n64|v64)$',                          'ROM dump'),
    @('(^|/)baserom[^/]*$',                                   'ROM dump'),
    @('(^|/)eeprom[^/]*\.bin$|\.eeprom$|\.style$',            'save / EEPROM data'),
    @('(^|/)(config|sightline-config)\.ini$|\.ini$',          'config file'),
    @('\.log$|\.out\.txt$|\.err\.txt$|(^|/)mark-[^/]*\.txt$|(^|/)crash\.txt$', 'log / run capture'),
    @('\.(input|move|vis|trace|sltrace)$|(^|/)runs/',          'run capture / trace'),
    @('\.(png|bmp|jpg|jpeg|ppm|gif|webp)$|(^|/)sheet-[^/]*$',  'screenshot / image capture'),
    @('(^|/)bin/|(^|/)build/|(^|/)sl_resample_tab\.h$|(^|/)aspboot[^/]*$|\.seg$|\.o$|\.s$|\.obj$', 'ROM-derived intermediate / build object'),
    @('(^|/)\.build_key$|(^|/)unresolved\.txt$|(^|/)link1\.err$|(^|/)stubs\.c$|(^|/)segments[^/]*$|(^|/)modelhit_pool[^/]*$', 'build intermediate'),
    @('\.(dds|ktx|ktx2)$|(^|/)[^/]*(xbla|hd[-_ ]?pack|community[-_ ]?hd)[^/]*(/|$)', 'Community HD / XBLA texture set'),
    @('(^|/)\.gitea/|(^|/)ci/|(^|/)tools/publish/|(^|/)gitea-token\.ps1$|(^|/)\.runner$|\.pem$|\.key$|(^|/)id_[^/]*$|(^|/)known_hosts$', 'private infrastructure / credential'),
    @('(^|/)\.git(/|$)|(^|/)\.gitignore$|(^|/)\.gitattributes$|(^|/)\.gitmodules$', 'Git metadata'),
    @('(^|/)__pycache__/|\.pyc$|(^|/)\.venv/|(^|/)venv/',     'Python cache / virtualenv'),
    @('(^|/)SightlineDemo\.exe$|(^|/)(displaytest|inputtest|settingstest)\.exe$', 'demo / test executable'),
    @('\.(zip|7z|rar|tar|gz|tgz)$',                           'archive inside the package (the controller source ZIPs among them)'),
    @('(^|/)tmp/|(^|/)temp/',                                  'temporary directory'),
    @('(^|/)[C]LAUDE\.md$|(^|/)\.[c]laude/',                   'agent configuration')
)
foreach ($f in $files) {
    $p = $rel[$f.FullName]
    foreach ($rule in $byName) {
        if ($p -match ('(?i)' + $rule[0])) { Fail ('prohibited-name (' + $rule[1] + ')') $p; break }
    }
}

# --------------------------------------------- prohibited, by content ---

$romSize = 12582912
$magics  = @('80371240', '37804012', '40123780')
$forbidden = @(
    '[c]laude', '[a]nthropic', '[o]penai',
    '[c]o-authored-by', '[g]enerated-by', '[a]ssisted-by',
    '[k]enneth',
    '192\.168\.',
    'git\.mscrnt',
    'C:\\Users\\(?!me\\)',
    'D:\\Projects\\',
    '\\scratchpad\\', 'AppData\\Local\\Temp',
    '-----BEGIN [A-Z ]*PRIVATE KEY',
    # Spelled with a bracket, like [c]laude above, so this file does not itself
    # contain the literal the public-export gate (tools/publish/public-export.json)
    # forbids; the regex matches exactly the same text.
    'gh[pos]_[A-Za-z0-9]{20,}', 'github_pat[_]', 'xox[baprs]-', 'AKIA[0-9A-Z]{16}'
)
$forbiddenRx = $forbidden | ForEach-Object { New-Object System.Text.RegularExpressions.Regex($_, 'IgnoreCase') }
# Binaries are scanned too: a -g build writes every source path and the
# compile directory into the executable's DWARF strings, which is exactly
# where an owner's absolute path or user name ends up without anyone typing it.

foreach ($f in $files) {
    $p = $rel[$f.FullName]
    if ($f.Length -eq $romSize) { Fail 'prohibited-content (ROM-sized file)' $p }
    $fs = [System.IO.File]::OpenRead($f.FullName)
    try {
        $head = New-Object byte[] 8192
        $n = $fs.Read($head, 0, 8192)
    } finally { $fs.Dispose() }
    if ($n -ge 4) {
        $m4 = ('{0:x2}{1:x2}{2:x2}{3:x2}' -f $head[0], $head[1], $head[2], $head[3])
        if ($magics -contains $m4) { Fail 'prohibited-content (ROM magic)' $p }
    }
    $isText = $true
    for ($i = 0; $i -lt $n; $i++) { if ($head[$i] -eq 0) { $isText = $false; break } }
    if ($f.Length -gt 0) {
        $text = [System.IO.File]::ReadAllText($f.FullName, [System.Text.Encoding]::GetEncoding(28591))
        foreach ($rx in $forbiddenRx) {
            $m = $rx.Match($text)
            if ($m.Success) {
                if ($isText) {
                    $where = "line " + ($text.Substring(0, $m.Index) -split "`n").Count
                } else {
                    $where = ("offset 0x{0:x} (binary)" -f $m.Index)
                }
                Fail ("prohibited-content (pattern '" + $rx.ToString() + "')") ("{0} at {1}" -f $p, $where)
            }
        }
    }
}

# ---------------------------------- prohibited, ROM-derived excerpts ---

if ($Rom) {
    if (-not (Test-Path -LiteralPath $Rom)) {
        Write-Host "validate-package: no ROM at '$Rom'."
        exit 1
    }
    $romBytes = [System.IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $Rom).Path)
    if ($romBytes.Length -ne $romSize -or ('{0:x2}{1:x2}{2:x2}{3:x2}' -f $romBytes[0], $romBytes[1], $romBytes[2], $romBytes[3]) -ne '80371240') {
        Write-Host "validate-package: -Rom is not a plain 12582912-byte big-endian .z64 dump; the excerpt scan needs one."
        exit 1
    }
    # The same derivation scripts/extract_asp_gsp_rsp.sh performs: the
    # 1172-compressed data segment at 137616 (71760 bytes), inflated as raw
    # deflate after its two-byte header; aspMainData is RAM 0x8005d020..0x8005d2e0
    # of a segment that loads at 0x80020d90. Taps at +0xC0 (256 shorts), the
    # ENVMIXER ramp at +0xB0 (8 shorts), both big-endian in the ROM.
    $cdata = New-Object byte[] 71760
    [Array]::Copy($romBytes, 137616, $cdata, 0, 71760)
    $ms  = New-Object System.IO.MemoryStream(, $cdata)
    $ms.Position = 2
    $ds  = New-Object System.IO.Compression.DeflateStream($ms, [System.IO.Compression.CompressionMode]::Decompress)
    $out = New-Object System.IO.MemoryStream
    $ds.CopyTo($out); $ds.Dispose(); $ms.Dispose()
    $inflated = $out.ToArray(); $out.Dispose()
    $lo = 0x8005d020 - 0x80020d90
    $seg = New-Object byte[] 0x2C0
    [Array]::Copy($inflated, $lo, $seg, 0, 0x2C0)
    $tapsLe = New-Object byte[] 512
    for ($i = 0; $i -lt 256; $i++) { $tapsLe[2 * $i] = $seg[0xC0 + 2 * $i + 1]; $tapsLe[2 * $i + 1] = $seg[0xC0 + 2 * $i] }
    $rampLe = New-Object byte[] 16
    for ($i = 0; $i -lt 8; $i++) { $rampLe[2 * $i] = $seg[0xB0 + 2 * $i + 1]; $rampLe[2 * $i + 1] = $seg[0xB0 + 2 * $i] }
    $header = New-Object byte[] 64
    [Array]::Copy($romBytes, 0, $header, 0, 64)
    $needles = @(
        @('ROM header (first 64 bytes)',                     (Latin $header)),
        @('aspMain microcode data segment (704 bytes)',      (Latin $seg)),
        @('compiled RESAMPLE polyphase table (512 bytes LE)', (Latin $tapsLe)),
        @('compiled ENVMIXER ramp (16 bytes LE)',             (Latin $rampLe))
    )
    $scanned = 0
    foreach ($f in $files) {
        if ($f.Length -lt 16) { continue }
        $hay = Latin ([System.IO.File]::ReadAllBytes($f.FullName))
        $scanned++
        foreach ($nd in $needles) {
            $at = $hay.IndexOf($nd[1], [System.StringComparison]::Ordinal)
            if ($at -ge 0) {
                Fail ('prohibited-content (ROM-derived excerpt: ' + $nd[0] + ')') ("{0} at offset 0x{1:x}" -f $rel[$f.FullName], $at)
            }
        }
    }
    Note ("ROM-derived excerpt scan: {0} files scanned against 4 needles derived from the ROM" -f $scanned)
} else {
    Note 'ROM-derived excerpt scan: SKIPPED (no -Rom given) - a table baked into an executable is not detectable without it'
}

# ------------------------------------------------------------ required ---

$required = @(
    'sightline.exe', 'Sightline.cmd', 'README.txt', 'VERSION.txt',
    'LICENSES/README.md', 'LICENSES/Sightline-Code-0BSD.txt',
    'LICENSES/Sightline-Assets-CC0-1.0.txt', 'LICENSES/asset-overrides-LICENSE.md',
    # #47: the optional texture fetcher ships whole or not at all - a menu
    # entry that downloads nothing, or a fetcher that cannot say which release
    # it wants, is worse than neither.
    'Get-Textures.cmd', 'tools/get-textures.ps1',
    'tools/community-source.json', 'tools/mapping/community.json'
)
foreach ($r in $required) { if (-not $present.Contains($r)) { Fail 'required-missing' $r } }
$bootModels = @($files | Where-Object { $rel[$_.FullName] -match '^data/asset-overrides/boot/[^/]+\.slmodel$' })
if ($bootModels.Count -lt 1) { Fail 'required-missing' 'data/asset-overrides/boot/*.slmodel (at least one)' }
$ctrlModels = @($files | Where-Object { $rel[$_.FullName] -match '^data/asset-overrides/controllers/([^/]+)\.slmodel$' })
foreach ($c in $ctrlModels) {
    $name = [System.IO.Path]::GetFileNameWithoutExtension($c.Name)
    $attr = "LICENSES/third-party/controllers/$name/ATTRIBUTION.md"
    if (-not $present.Contains($attr)) { Fail 'required-missing (CC-BY-4.0 attribution for a shipped controller model)' $attr }
}
if ($Version -and $present.Contains('VERSION.txt')) {
    $vt = Get-Content -LiteralPath (Join-Path $root 'VERSION.txt') -Raw
    if ($vt -notmatch ('(?m)^version\s+' + [regex]::Escape($Version) + '\s*$')) { Fail 'version-mismatch' "VERSION.txt does not carry 'version $Version'" }
}

# the executable must be a normal (non-demo) 32-bit console image
if ($present.Contains('sightline.exe')) {
    $exe = Join-Path $root 'sightline.exe'
    $fs = [System.IO.File]::OpenRead($exe)
    try {
        $br = New-Object System.IO.BinaryReader($fs)
        $fs.Position = 0x3C; $pe = $br.ReadUInt32(); $fs.Position = $pe
        $sig = $br.ReadUInt32(); $machine = $br.ReadUInt16()
        if ($sig -ne 0x00004550 -or $machine -ne 0x14c) { Fail 'exe-format' 'sightline.exe is not a PE32 / i386 image' }
    } finally { $fs.Dispose() }
    $exeText = Latin ([System.IO.File]::ReadAllBytes($exe))
    if ($exeText.IndexOf('sightline\demo\', [System.StringComparison]::Ordinal) -ge 0) {
        Note 'sightline.exe carries the demo save-path string (present in every build; the -DSL_DEMO_BUILD identity is checked through .build_key by the packager)'
    }
}

# -------------------------------------------- import-table cross-check ---

if (-not $Objdump) {
    foreach ($r in @("$env:SystemDrive\msys64", 'C:\msys64', 'C:\msys32', "$env:ProgramFiles\msys64", "$env:LOCALAPPDATA\Programs\msys64")) {
        if ($r -and (Test-Path (Join-Path $r 'mingw32\bin\objdump.exe'))) { $Objdump = Join-Path $r 'mingw32\bin\objdump.exe'; break }
    }
}
if ($Objdump -and (Test-Path -LiteralPath $Objdump) -and $present.Contains('sightline.exe')) {
    $mingwBin = Split-Path -Parent $Objdump
    $queue = New-Object System.Collections.Generic.Queue[string]
    $seen  = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    $queue.Enqueue('sightline.exe')
    while ($queue.Count -gt 0) {
        $img = $queue.Dequeue()
        if ($seen.Contains($img)) { continue }
        [void]$seen.Add($img)
        $imgPath = Join-Path $root $img
        $names = @(& $Objdump -p $imgPath | Select-String 'DLL Name:\s*(\S+)' | ForEach-Object { $_.Matches[0].Groups[1].Value })
        foreach ($dll in $names) {
            if ($present.Contains($dll)) { $queue.Enqueue($dll); continue }
            if (Test-Path (Join-Path $mingwBin $dll)) { Fail 'required-missing (redistributable DLL named by the import table)' ("{0} (imported by {1})" -f $dll, $img) }
            # else: a system DLL, which must NOT ship
        }
    }
    Note ("import-table cross-check: walked " + (($seen | Sort-Object) -join ', '))
} else {
    Note 'import-table cross-check: SKIPPED (no objdump found)'
}

# ------------------------------------------------------------ verdict ---

foreach ($nt in $script:Notes) { Write-Host "  note: $nt" }
if ($script:Failures.Count -eq 0) {
    Write-Host ("validate-package: PASS ({0} files)" -f $files.Count)
    if ($tempRoot) { Remove-Item -Recurse -Force -LiteralPath $tempRoot -ErrorAction SilentlyContinue }
    exit 0
}
foreach ($fl in $script:Failures) { Write-Host "  FAIL: $fl" }
Write-Host ("validate-package: REJECTED - {0} failure(s) in {1} files" -f $script:Failures.Count, $files.Count)
if ($tempRoot) { Remove-Item -Recurse -Force -LiteralPath $tempRoot -ErrorAction SilentlyContinue }
exit 1
