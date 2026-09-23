<#
.SYNOPSIS
    Obtain the Community HD texture pack from its maintainers and convert it,
    on this machine, into the form Sightline reads.

.DESCRIPTION
    One command, no repository and no build toolchain required:

        .\Get-Textures.cmd                      (in a release package)
        .\tools\texpack\get-textures.ps1        (in a source checkout)

    It downloads the pinned official release of the Community HD project

        https://github.com/GhostlyDark/GoldenEye-007-HD

    straight from its maintainers to YOUR disk, verifies size and SHA-256
    against the identities recorded in community-source.json, and converts the
    textures the mapping table names into <root>\community\<hex4>.sltx, which
    is what the TEXTURES = COMMUNITY HD setting reads.

    SIGHTLINE DISTRIBUTES NO TEXTURE. Nothing is mirrored from Sightline
    infrastructure, no pack bytes are shipped in any package, and the download
    is between you and the pack's own maintainers - which is precisely the
    arrangement they asked for when they declined redistribution: a local
    adapter for their official release.

    Re-running is cheap and safe. An archive already in the cache whose size
    and SHA-256 match the pin is used as it stands, an interrupted download
    resumes where it stopped, and every converted file is written through a
    temporary and renamed into place.

    THE XBLA SET IS NOT OBTAINED BY THIS OR ANY OTHER TOOL. It is
    user-supplied, Sightline neither ships, locates nor downloads a source,
    and nothing here points at one.

.PARAMETER Out
    The pack root to write under - the tool writes <Out>\community\. Defaults
    to the player-data root the game reads when SL_TEXPACK_ROOT is not set:
    %LOCALAPPDATA%\sightline\assets\texpacks.

.PARAMETER Cache
    Where the downloaded archive is kept, so a second run needs no network.
    Defaults to %LOCALAPPDATA%\sightline\cache.

.PARAMETER Archive
    Use this archive instead of downloading - your own copy of the official
    release. Verified against the pin unless -AnyArchive is given.

.PARAMETER FetchOnly
    Download and verify, print the archive's path, convert nothing.

.PARAMETER NoDownload
    Never touch the network: use a cached or given archive or stop.

.PARAMETER AllowUnpinned
    Accept a release other than the pinned one (the newest whose assets match
    the pattern in community-source.json). The pin cannot vouch for its
    contents, so this is never the default and the tool says what it took.

.PARAMETER AnyArchive
    Accept -Archive without matching the pinned SHA-256. For trying a copy the
    pin does not know; the conversion still refuses anything it cannot read.
#>
[CmdletBinding()]
param(
    [string]$Out,
    [string]$Cache,
    [string]$Archive,
    [switch]$FetchOnly,
    [switch]$NoDownload,
    [switch]$AllowUnpinned,
    [switch]$AnyArchive,
    [switch]$Quiet,
    [string]$Mapping
)

$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot

# Before the first request of any kind, not just the download: an older
# Windows PowerShell still defaults to TLS 1.0, which github.com refuses, and
# the failure would land on the API call rather than anywhere informative.
try { [Net.ServicePointManager]::SecurityProtocol =
      [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12 } catch { }

function Say { param([string]$T) if (-not $Quiet) { Write-Host $T } }
function Die {
    param([string[]]$Lines)
    Write-Host ''
    foreach ($l in $Lines) { Write-Host $l }
    Write-Host ''
    exit 1
}

# --------------------------------------------------------------- the pin ---

$pinPath = Join-Path $here 'community-source.json'
if (-not (Test-Path -LiteralPath $pinPath)) {
    Die @("get-textures: $pinPath is missing - this tool cannot tell which release it wants.")
}
$pin = Get-Content -Raw -LiteralPath $pinPath | ConvertFrom-Json
$mapPath = $Mapping
if (-not $mapPath) { $mapPath = Join-Path $here 'mapping\community.json' }

if (-not $Out) {
    if (-not $env:LOCALAPPDATA) { Die @('get-textures: no %LOCALAPPDATA% to write the pack under; pass -Out <dir>.') }
    $Out = Join-Path $env:LOCALAPPDATA 'sightline\assets\texpacks'
}
if (-not $Cache) {
    if ($env:LOCALAPPDATA) { $Cache = Join-Path $env:LOCALAPPDATA 'sightline\cache' }
    else { $Cache = Join-Path $env:TEMP 'sightline-cache' }
}

# ------------------------------------------------------------ the fetch ----

function Get-Sha256 {
    param([string]$P)
    return (Get-FileHash -LiteralPath $P -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Test-Pinned {
    param([string]$P)
    if (-not (Test-Path -LiteralPath $P)) { return $false }
    if ((Get-Item -LiteralPath $P).Length -ne [int64]$pin.pinned.size) { return $false }
    return ((Get-Sha256 $P) -eq $pin.pinned.sha256.ToLowerInvariant())
}

function Invoke-GitHubApi {
    param([string]$Url)
    $h = @{ 'User-Agent' = 'sightline-get-textures'; 'Accept' = 'application/vnd.github+json' }
    return Invoke-RestMethod -Uri $Url -Headers $h -UseBasicParsing -TimeoutSec 60
}

function Resolve-Asset {
    <# Ask the upstream project itself what it publishes. The PINNED release is
       asked for by tag first - a release discovered by "latest" drifts under a
       mapping table that was made against one - and the listing is consulted
       only to say something useful when the pin is gone. #>
    $tag = $pin.pinned.tag
    $rel = $null
    try {
        $rel = Invoke-GitHubApi ("{0}/releases/tags/{1}" -f $pin.api, $tag)
    } catch {
        $rel = $null
    }
    if ($rel) {
        $a = @($rel.assets | Where-Object { $_.name -eq $pin.pinned.asset })
        if ($a.Count -eq 1) {
            return [pscustomobject]@{ Tag = $rel.tag_name; Name = $a[0].name; Size = $a[0].size
                                      Url = $a[0].browser_download_url; Digest = $a[0].digest; Pinned = $true }
        }
    }

    # The pinned release, or its asset, is not there any more.
    $listing = $null
    try { $listing = @(Invoke-GitHubApi ("{0}/releases" -f $pin.api)) } catch { $listing = $null }

    if ($AllowUnpinned -and $listing) {
        foreach ($r in $listing) {
            $a = @($r.assets | Where-Object { $_.name -like $pin.asset_pattern })
            if ($a.Count -ge 1) {
                return [pscustomobject]@{ Tag = $r.tag_name; Name = $a[0].name; Size = $a[0].size
                                          Url = $a[0].browser_download_url; Digest = $a[0].digest; Pinned = $false }
            }
        }
    }

    $lines = @("get-textures: the Community HD release this tool is pinned to is not available.",
               "  wanted: $($pin.pinned.asset)  (release $tag of $($pin.repo))")
    if ($null -eq $listing) {
        $lines += "  The upstream release list could not be read either - check the network, a proxy,"
        $lines += "  or whether github.com is reachable from here, and run this again."
    } elseif ($listing.Count -eq 0) {
        $lines += "  That project currently publishes no releases at all."
    } else {
        $lines += "  It currently publishes: " + (($listing | ForEach-Object { $_.tag_name }) -join ', ')
        $lines += "  Re-run with -AllowUnpinned to take the newest matching asset instead (its"
        $lines += "  contents cannot be checked against the recorded SHA-256), or update the pin."
    }
    $lines += "  The pack and its releases live at $($pin.upstream) - Sightline hosts no copy."
    Die $lines
}

function Get-Upstream {
    param([pscustomobject]$Asset, [string]$Dest)
    $part = $Dest + '.part'
    $have = 0L
    if (Test-Path -LiteralPath $part) { $have = (Get-Item -LiteralPath $part).Length }
    if ($have -ge $Asset.Size) { Remove-Item -LiteralPath $part -Force; $have = 0L }

    $req = [System.Net.HttpWebRequest]::Create($Asset.Url)
    $req.UserAgent = 'sightline-get-textures'
    $req.Timeout = 60000
    $req.ReadWriteTimeout = 120000
    $appending = $false
    if ($have -gt 0) { $req.AddRange($have); $appending = $true }

    $resp = $null
    try { $resp = $req.GetResponse() }
    catch {
        Die @("get-textures: the download could not be started.",
              "  $($_.Exception.Message)",
              "  The pack is published at $($pin.upstream) - Sightline hosts no copy.")
    }
    if ($appending -and [int]$resp.StatusCode -ne 206) { $have = 0L; $appending = $false }

    $total = $have + $resp.ContentLength
    $mode = 'Create'; if ($appending) { $mode = 'Append' }
    $fs = New-Object System.IO.FileStream($part, [System.IO.FileMode]$mode, [System.IO.FileAccess]::Write)
    $rs = $resp.GetResponseStream()
    $buf = New-Object byte[] 262144
    $done = $have
    $next = 0L
    $t0 = Get-Date
    try {
        while ($true) {
            $n = $rs.Read($buf, 0, $buf.Length)
            if ($n -le 0) { break }
            $fs.Write($buf, 0, $n)
            $done += $n
            if ($done -ge $next) {
                $next = $done + 4194304
                if (-not $Quiet) {
                    $pct = 0; if ($total -gt 0) { $pct = [int](100 * $done / $total) }
                    Write-Host -NoNewline ("`r  {0,3}%  {1,6:N1} of {2:N1} MB" -f $pct, ($done / 1MB), ($total / 1MB))
                }
            }
        }
    } finally {
        $fs.Dispose(); $rs.Dispose(); $resp.Close()
    }
    if (-not $Quiet) {
        $secs = ((Get-Date) - $t0).TotalSeconds
        Write-Host ("`r  100%  {0,6:N1} of {1:N1} MB in {2:N0}s          " -f ($done / 1MB), ($total / 1MB), $secs)
    }

    if ($done -ne $Asset.Size) {
        Die @("get-textures: the download is $done bytes, the release says $($Asset.Size).",
              "  The partial file is kept at $part and the next run resumes it.")
    }
    $got = Get-Sha256 $part
    if ($Asset.Pinned -and $got -ne $pin.pinned.sha256.ToLowerInvariant()) {
        Remove-Item -LiteralPath $part -Force
        Die @("get-textures: the downloaded archive is not the one this tool is pinned to.",
              "  expected sha256 $($pin.pinned.sha256)",
              "  got      sha256 $got",
              "  It has been deleted rather than used. Run this again; if it keeps happening,",
              "  the upstream release has been replaced and the pin needs updating.")
    }
    if (Test-Path -LiteralPath $Dest) { Remove-Item -LiteralPath $Dest -Force }
    Move-Item -LiteralPath $part -Destination $Dest
    return $got
}

# -------------------------------------------------------- the conversion ---

$csharp = @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Runtime.InteropServices;

public static class SlTexPack
{
    // PNG -> tightly packed RGBA8, rows top first, written as one SLTX file
    // (docs/texture-packs.md). Returns {width, height, written}: a
    // replacement outside the runtime's bound is measured and NOT written.
    public static int[] WriteSltx(byte[] png, string path, int id, int n64w, int n64h, int maxDim)
    {
        int w, h;
        byte[] rgba;
        using (MemoryStream ms = new MemoryStream(png, false))
        using (Bitmap bmp = new Bitmap(ms))
        {
            w = bmp.Width;
            h = bmp.Height;
            if (w <= 0 || h <= 0 || w > maxDim || h > maxDim)
                return new int[] { w, h, 0 };
            rgba = new byte[w * h * 4];
            BitmapData bd = bmp.LockBits(new Rectangle(0, 0, w, h),
                                         ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            try
            {
                byte[] row = new byte[w * 4];
                for (int y = 0; y < h; y++)
                {
                    Marshal.Copy((IntPtr)(bd.Scan0.ToInt64() + (long)y * bd.Stride), row, 0, w * 4);
                    int o = y * w * 4;
                    for (int x = 0; x < w; x++)
                    {
                        // GDI+ hands back B,G,R,A little-endian; SLTX is R,G,B,A
                        rgba[o + x * 4 + 0] = row[x * 4 + 2];
                        rgba[o + x * 4 + 1] = row[x * 4 + 1];
                        rgba[o + x * 4 + 2] = row[x * 4 + 0];
                        rgba[o + x * 4 + 3] = row[x * 4 + 3];
                    }
                }
            }
            finally { bmp.UnlockBits(bd); }
        }

        byte[] head = new byte[28];
        head[0] = (byte)'S'; head[1] = (byte)'L'; head[2] = (byte)'T'; head[3] = (byte)'X';
        Put32(head, 4, 1);
        Put32(head, 8, (uint)id);
        Put16(head, 12, (ushort)n64w); Put16(head, 14, (ushort)n64h);
        Put16(head, 16, (ushort)w);    Put16(head, 18, (ushort)h);
        Put32(head, 20, 0);
        Put32(head, 24, (uint)(w * h * 4));

        string dir = Path.GetDirectoryName(path);
        if (dir.Length > 0) Directory.CreateDirectory(dir);
        string tmp = path + ".part";
        using (FileStream fs = new FileStream(tmp, FileMode.Create, FileAccess.Write))
        {
            fs.Write(head, 0, head.Length);
            fs.Write(rgba, 0, rgba.Length);
        }
        if (File.Exists(path)) File.Delete(path);
        File.Move(tmp, path);
        return new int[] { w, h, 1 };
    }

    static void Put16(byte[] b, int o, ushort v) { b[o] = (byte)v; b[o + 1] = (byte)(v >> 8); }
    static void Put32(byte[] b, int o, uint v)
    { b[o] = (byte)v; b[o + 1] = (byte)(v >> 8); b[o + 2] = (byte)(v >> 16); b[o + 3] = (byte)(v >> 24); }
}
'@

function Convert-Community {
    param([string]$Zip, [string]$Root)

    if (-not (Test-Path -LiteralPath $mapPath)) {
        Die @("get-textures: the mapping table $mapPath is missing.")
    }
    $map = Get-Content -Raw -LiteralPath $mapPath | ConvertFrom-Json
    if ($map.version -ne 1) { Die @("get-textures: $mapPath is version $($map.version), this tool reads 1.") }

    Add-Type -AssemblyName System.Drawing
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    if (-not ([System.Management.Automation.PSTypeName]'SlTexPack').Type) {
        Add-Type -TypeDefinition $csharp -ReferencedAssemblies System.Drawing
    }

    $z = [System.IO.Compression.ZipFile]::OpenRead($Zip)
    try {
        # index the pack by the texture checksum its filenames encode:
        # <prefix>#<texCRC>#<fmt>#<size>[#<palCRC>]_all.png
        $byCrc = @{}
        foreach ($e in $z.Entries) {
            $base = $e.Name
            if (-not $base.ToLowerInvariant().EndsWith('.png')) { continue }
            $parts = $base.Split('#')
            if ($parts.Length -lt 3) { continue }
            $k = $parts[1].ToUpperInvariant()
            if (-not $byCrc.ContainsKey($k)) { $byCrc[$k] = New-Object System.Collections.Generic.List[object] }
            $byCrc[$k].Add($e)
        }
        if ($byCrc.Count -eq 0) {
            Die @("get-textures: $Zip holds no hi-res texture files this tool recognises",
                  "  (expected names carrying #<checksum>#). Is it the PNG variant of the release?")
        }

        $written = 0; $absent = 0; $skipped = New-Object System.Collections.Generic.List[string]
        $total = @($map.entries).Count
        $seen = 0
        foreach ($m in ($map.entries | Sort-Object { $_.id })) {
            $seen++
            $cands = $byCrc[$m.crc.ToUpperInvariant()]
            if ($null -eq $cands) { $absent++; continue }
            $entry = $cands[0]
            if ($cands.Count -gt 1) {
                # deterministic on a duplicate: the ordinally first full name
                $names = @($cands | ForEach-Object { $_.FullName })
                [Array]::Sort($names, [StringComparer]::Ordinal)
                $entry = @($cands | Where-Object { $_.FullName -eq $names[0] })[0]
            }
            $ms = New-Object System.IO.MemoryStream
            $s = $entry.Open()
            try { $s.CopyTo($ms) } finally { $s.Dispose() }
            $tid = [Convert]::ToInt32($m.id, 16)
            $dst = Join-Path $Root ('community\{0}.sltx' -f $m.id.ToLowerInvariant())
            try {
                $r = [SlTexPack]::WriteSltx($ms.ToArray(), $dst, $tid, [int]$m.n64[0], [int]$m.n64[1], 1024)
            } catch {
                $skipped.Add(("{0}: {1}" -f $m.id, $_.Exception.Message))
                $ms.Dispose(); continue
            }
            $ms.Dispose()
            if ($r[2] -eq 1) { $written++ }
            else { $skipped.Add(("{0} {1}x{2} exceeds the runtime bound" -f $m.id, $r[0], $r[1])) }
            if (-not $Quiet -and ($seen % 25 -eq 0)) {
                Write-Host -NoNewline ("`r  {0,4} of {1} textures" -f $seen, $total)
            }
        }
        if (-not $Quiet) { Write-Host ("`r  {0,4} of {1} textures    " -f $seen, $total) }
        return [pscustomobject]@{ Written = $written; Absent = $absent; Total = $total; Skipped = $skipped }
    } finally { $z.Dispose() }
}

# ------------------------------------------------------------------ run ----

Say ''
Say 'Sightline - Community HD textures'
Say '---------------------------------'
Say "  from    $($pin.upstream)"
Say "  release $($pin.pinned.tag)  $($pin.pinned.asset)"
if (-not $FetchOnly) { Say "  into    $Out\community" }
Say ''

if ($Archive) {
    if (-not (Test-Path -LiteralPath $Archive)) { Die @("get-textures: no archive at $Archive.") }
    $src = (Resolve-Path -LiteralPath $Archive).Path
    if (-not $AnyArchive -and -not (Test-Pinned $src)) {
        Die @("get-textures: $src is not the release this tool is pinned to.",
              "  expected $($pin.pinned.size) bytes, sha256 $($pin.pinned.sha256)",
              "  Pass -AnyArchive to use it anyway, or let the tool download the pinned release.")
    }
    Say "  using the archive you named"
} else {
    New-Item -ItemType Directory -Force -Path $Cache | Out-Null
    $src = Join-Path $Cache $pin.local_name
    if (Test-Pinned $src) {
        Say "  already downloaded and verified: $src"
    } elseif ($NoDownload) {
        Die @("get-textures: no verified copy of the pack in $Cache and -NoDownload was given.")
    } else {
        $asset = Resolve-Asset
        if ($asset.Pinned) { Say "  upstream release $($asset.Tag) found" }
        else { Say "  UNPINNED: taking $($asset.Name) from release $($asset.Tag) - the recorded SHA-256 cannot vouch for it" }
        Say "  downloading $($asset.Name)  ($([int]($asset.Size / 1MB)) MB, once)"
        $h = Get-Upstream -Asset $asset -Dest $src
        Say "  verified sha256 $h"
    }
}

if ($FetchOnly) {
    Say ''
    Write-Output $src
    exit 0
}

Say ''
Say '  converting (no pixel of this leaves your machine, and none is shipped)'
$r = Convert-Community -Zip $src -Root $Out
foreach ($s in ($r.Skipped | Select-Object -First 10)) { Say "  skipped $s" }
if ($r.Skipped.Count -gt 10) { Say "  ... and $($r.Skipped.Count - 10) more" }

Say ''
Say ("  community  {0,4} of {1} mapped textures  ->  {2}" -f $r.Written, $r.Total, (Join-Path $Out 'community'))
if ($r.Absent -gt 0) { Say ("             {0,4} are not in this copy of the pack; the game draws its own artwork there" -f $r.Absent) }
Say ''
Say '  Start the game and choose OPTIONS > SETTINGS > DISPLAY > TEXTURES = COMMUNITY HD'
Say '  (or the watch''s SIGHTLINE > GRAPHICS page, in a mission).'
Say ''
Say '  The XBLA set is not obtained by this tool: it is user-supplied, and'
Say '  Sightline neither ships, locates nor downloads a source for it.'
Say ''
if ($r.Written -eq 0) { exit 1 }
exit 0
