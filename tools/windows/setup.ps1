<#
.SYNOPSIS
    Check (and optionally install) everything the Windows Sightline build needs.

.DESCRIPTION
    Run from an ordinary Windows PowerShell prompt at the repository root:

        .\tools\windows\setup.ps1            # diagnose
        .\tools\windows\setup.ps1 -Install   # install what is missing

    Every requirement is validated by QUERYING THE EXECUTABLE, not by testing
    that a path exists. The compiler check ends in a real compile-and-link of a
    probe program whose PE header is then read back, so "satisfied" means the
    toolchain actually produced a PE32 / i386 / LARGE_ADDRESS_AWARE image with
    SDL2 linked - the same thing build.ps1 needs it to do.

    MSYS2 is a TOOLCHAIN PROVIDER only. Nothing here asks you to open an MSYS2
    shell, and nothing is downloaded into the repository: no toolchains, no
    DLLs, no archives.

.PARAMETER Install
    Install missing MSYS2 packages with pacman. Without it, setup only reports.

.PARAMETER Msys2Root
    Where MSYS2 lives. When given (or when SL_MSYS2_ROOT is set) it is
    AUTHORITATIVE - setup will not quietly fall back to another installation,
    because reporting on a different toolchain than the one you named is the
    kind of silent wrong answer this project exists to avoid.

.PARAMETER Yes
    Do not prompt before installing.
#>
[CmdletBinding()]
param(
    [switch]$Install,
    [switch]$Yes,
    [string]$Msys2Root
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

$script:Failures = New-Object System.Collections.Generic.List[string]
$script:Packages = New-Object System.Collections.Generic.List[string]

function Write-Check {
    param([bool]$Ok, [string]$Name, [string]$Detail)
    if ($Ok) { $tag = '  ok   ' } else { $tag = '  MISS ' }
    Write-Host ("{0}{1,-26} {2}" -f $tag, $Name, $Detail)
}

function Add-Failure {
    param([string]$Message, [string]$Package)
    $script:Failures.Add($Message)
    if ($Package -and -not $script:Packages.Contains($Package)) {
        $script:Packages.Add($Package)
    }
}

function Exit-Setup {
    # Every exit goes through here so the PATH this script prepends can never
    # leak into the calling shell.
    param([int]$Code)
    if ($null -ne $script:SavedPath) { $env:PATH = $script:SavedPath }
    exit $Code
}

function Get-PeFacts {
    # Machine, optional-header magic and Characteristics, straight out of the
    # file. No dependency on objdump, so this works before binutils is proven.
    param([string]$Path)
    $fs = [System.IO.File]::OpenRead($Path)
    try {
        $br = New-Object System.IO.BinaryReader($fs)
        $fs.Position = 0x3C
        $peOff = $br.ReadUInt32()
        $fs.Position = $peOff
        $sig = $br.ReadUInt32()
        if ($sig -ne 0x00004550) { return $null }   # 'PE\0\0'
        $machine = $br.ReadUInt16()
        $null = $br.ReadUInt16()                    # NumberOfSections
        $null = $br.ReadUInt32(); $null = $br.ReadUInt32(); $null = $br.ReadUInt32()
        $null = $br.ReadUInt16()                    # SizeOfOptionalHeader
        $chars = $br.ReadUInt16()
        $magic = $br.ReadUInt16()
        return New-Object PSObject -Property @{
            Machine = $machine; Characteristics = $chars; Magic = $magic
        }
    } finally { $fs.Dispose() }
}

Write-Host ''
Write-Host 'Sightline - Windows toolchain check'
Write-Host '-----------------------------------'

# ------------------------------------------------------------------ shell --

$psOk = ($PSVersionTable.PSVersion.Major -ge 5)
Write-Check $psOk 'PowerShell' ("{0} ({1})" -f $PSVersionTable.PSVersion, $PSVersionTable.PSEdition)
if (-not $psOk) { Add-Failure 'PowerShell 5.1 or newer is required.' '' }
Write-Check $true 'host' ("{0}, {1}" -f [System.Environment]::OSVersion.VersionString,
                                        $env:PROCESSOR_ARCHITECTURE)
Write-Check $true 'repository' $repo

# ------------------------------------------------------------------ MSYS2 --

$explicitRoot = $Msys2Root
if (-not $explicitRoot) { $explicitRoot = $env:SL_MSYS2_ROOT }

$roots = New-Object System.Collections.Generic.List[string]
if ($explicitRoot) {
    $roots.Add($explicitRoot)
} else {
    foreach ($r in @("$env:SystemDrive\msys64", 'C:\msys64', 'C:\msys32',
                     "$env:ProgramFiles\msys64",
                     "$env:LOCALAPPDATA\Programs\msys64")) {
        if ($r) { $roots.Add($r) }
    }
}

$msysRoot = $null
foreach ($r in $roots) {
    if (Test-Path (Join-Path $r 'usr\bin\pacman.exe')) { $msysRoot = $r; break }
}
if ($null -eq $msysRoot) {
    foreach ($r in $roots) { if (Test-Path $r) { $msysRoot = $r; break } }
}

if ($null -eq $msysRoot) {
    if ($explicitRoot) {
        Write-Check $false 'MSYS2' "not found at $explicitRoot (you named it explicitly)"
    } else {
        Write-Check $false 'MSYS2' "not found in $($roots -join ', ')"
    }
    Add-Failure ("MSYS2 is not installed, or is somewhere setup did not look.`n" +
                 "    Install it with:  winget install --id MSYS2.MSYS2`n" +
                 "    or point setup at it:  .\tools\windows\setup.ps1 -Msys2Root D:\msys64`n" +
                 "    (SL_MSYS2_ROOT does the same thing for build.ps1 and play.ps1.)") ''
} else {
    Write-Check $true 'MSYS2 root' $msysRoot
}

# Even when nothing was found, carry on with the root the developer named (or
# the first candidate). Every later check then reports MISS against a REAL
# path instead of an empty one, and none of them silently vanish - a check
# that did not run has to say so, or its requirement looks satisfied.
if ($null -eq $msysRoot) { $msysRoot = $roots[0] }

$mingwBin = Join-Path $msysRoot 'mingw32\bin'
$pacman   = $null
$p = Join-Path $msysRoot 'usr\bin\pacman.exe'
if (Test-Path $p) { $pacman = $p }

# MEASURED: gcc's own copies of as.exe and ld.exe live under
# i686-w64-mingw32\bin and load their runtime DLLs from mingw32\bin. Without
# that directory on PATH gcc fails to assemble at all, and the sub-tools die
# with 0xC0000135 (STATUS_DLL_NOT_FOUND) printing nothing whatsoever - which
# reads exactly like a tool that ran and had nothing to say. build.ps1 sets
# the same PATH for the same reason; both restore it on the way out.
$script:SavedPath = $env:PATH
if ($mingwBin -and (Test-Path $mingwBin)) { $env:PATH = "$mingwBin;$env:PATH" }

# ------------------------------------------------------------- toolchain ---

$CC = $null
if ($mingwBin) {
    $cand = Join-Path $mingwBin 'i686-w64-mingw32-gcc.exe'
    if (Test-Path $cand) { $CC = $cand }
}
if ($null -eq $CC) {
    Write-Check $false 'i686 gcc' "no i686-w64-mingw32-gcc.exe under $mingwBin"
    Add-Failure 'The 32-bit MinGW C compiler is missing.' 'mingw-w64-i686-gcc'
} else {
    $target = & $CC -dumpmachine
    if ($LASTEXITCODE -ne 0) {
        Write-Check $false 'i686 gcc' "$CC would not run"
        Add-Failure "The compiler at $CC did not answer -dumpmachine." 'mingw-w64-i686-gcc'
        $CC = $null
    } elseif ($target -notlike 'i686-*') {
        Write-Check $false 'i686 gcc' "targets $target, not i686"
        Add-Failure ("$CC targets $target. An x86_64 driver silently emits PE32+" +
                     " and every 32-bit assumption in this tree would then be wrong.") 'mingw-w64-i686-gcc'
        $CC = $null
    } else {
        $ver = (& $CC -dumpversion)
        Write-Check $true 'i686 gcc' "$target, gcc $ver"
    }
}

# binutils, as gcc itself resolves them - not as whatever is on PATH.
if ($CC) {
    foreach ($prog in @('as', 'ld')) {
        $path = & $CC "-print-prog-name=$prog"
        $ok = ($path -and (Test-Path $path))
        if ($ok) {
            # RUN it, and insist it both exits 0 and says something. A tool that
            # cannot load its DLLs exits 0xC0000135 in total silence, and
            # "presence" would have called that a pass.
            $vline = (& $path --version | Select-Object -First 1)
            if ($LASTEXITCODE -ne 0 -or -not $vline) {
                Write-Check $false "binutils $prog" "$path exited $LASTEXITCODE without a version"
                Add-Failure "The '$prog' the compiler resolves to will not run." 'mingw-w64-i686-gcc'
            } else {
                Write-Check $true "binutils $prog" $vline
            }
        } else {
            Write-Check $false "binutils $prog" "gcc cannot resolve '$prog'"
            Add-Failure "The assembler/linker '$prog' is missing from the toolchain." 'mingw-w64-i686-gcc'
        }
    }
    foreach ($prog in @('objdump.exe', 'nm.exe')) {
        $path = Join-Path $mingwBin $prog
        if (Test-Path $path) {
            Write-Check $true "binutils $prog" ((& $path --version | Select-Object -First 1))
        } else {
            Write-Check $false "binutils $prog" "not in $mingwBin"
            Add-Failure "$prog is missing (build.ps1 reads the PE import table with it)." 'mingw-w64-i686-binutils'
        }
    }
}

# --------------------------------------------------------------- pkgconf ---

$pkgconf = $null
if ($mingwBin) {
    foreach ($nm in @('pkgconf.exe', 'pkg-config.exe')) {
        $cand = Join-Path $mingwBin $nm
        if (Test-Path $cand) { $pkgconf = $cand; break }
    }
}
if ($null -eq $pkgconf) {
    Write-Check $false 'pkgconf' "not in $mingwBin"
    Add-Failure 'pkgconf is missing; build.ps1 asks it for the SDL2 compile flags.' 'mingw-w64-i686-pkgconf'
} else {
    $pv = (& $pkgconf --version)
    Write-Check $true 'pkgconf' "$pkgconf ($pv)"
}

# ------------------------------------------------------------------ SDL2 ---

$sdlCflags = $null
if ($null -eq $pkgconf) {
    Write-Check $false 'SDL2 (dev)' 'not checked - there is no pkgconf to ask'
    Add-Failure 'SDL2 development files could not be checked without pkgconf.' 'mingw-w64-i686-SDL2'
} else {
    $sdlVer = & $pkgconf --modversion sdl2
    if ($LASTEXITCODE -ne 0) {
        Write-Check $false 'SDL2 (dev)' 'pkgconf does not know sdl2'
        Add-Failure 'The SDL2 development package is missing.' 'mingw-w64-i686-SDL2'
    } else {
        Write-Check $true 'SDL2 (dev)' "sdl2 $sdlVer"
        $sdlCflags = ((& $pkgconf --cflags sdl2) -join ' ')
    }
}

$dll = Join-Path $mingwBin 'SDL2.dll'
if (Test-Path $dll) {
    $f = Get-PeFacts $dll
    if ($null -ne $f -and $f.Machine -eq 0x14c) {
        Write-Check $true 'SDL2.dll' "$dll (machine 0x014c)"
    } else {
        Write-Check $false 'SDL2.dll' "$dll is not a 32-bit image"
        Add-Failure 'SDL2.dll beside the toolchain is not 32-bit.' 'mingw-w64-i686-SDL2'
    }
} else {
    Write-Check $false 'SDL2.dll' "not in $mingwBin"
    Add-Failure 'SDL2.dll is missing; build.ps1 stages it next to the exe.' 'mingw-w64-i686-SDL2'
}

# ------------------------------------------------------------------- sed ---

$sedExe = $null
$sedPath = Join-Path $msysRoot 'usr\bin\sed.exe'
if (Test-Path $sedPath) { $sedExe = $sedPath }
if ($null -eq $sedExe) {
    Write-Check $false 'sed' "not at $sedPath"
    Add-Failure ('sed is missing. It applies tools/native/aiprint.sed to' +
                 ' chraidata.c, the one transform build.ps1 does not own.') 'sed'
} else {
    Write-Check $true 'sed' ((& $sedExe --version | Select-Object -First 1))
}

# ---------------------------------------------------------------- python ---

$python = $env:SL_PYTHON
if (-not $python) { $python = Join-Path $repo '.venv\Scripts\python.exe' }
if (Test-Path $python) {
    Write-Check $true 'python' ("$python (" + (& $python -c "import sys;print(sys.version.split()[0])") + ')')
} else {
    $sys = Get-Command python.exe -ErrorAction SilentlyContinue
    if ($null -ne $sys) {
        Write-Check $true 'python' "$($sys.Source) (repo venv absent; SL_PYTHON overrides)"
        $python = $sys.Source
    } else {
        Write-Check $false 'python' 'no .venv\Scripts\python.exe and none on PATH'
        Add-Failure ("Python is needed for the generation steps.`n" +
                     "    Create the repo venv:  python -m venv .venv") ''
        $python = $null
    }
}

# ------------------------------------------------------------- map input ---

$mapFile = Join-Path $repo 'build\u\ge007.u.map'
if (Test-Path $mapFile) {
    Write-Check $true 'matching-build map' "build\u\ge007.u.map ($((Get-Item $mapFile).Length) bytes)"
} else {
    Write-Check $false 'matching-build map' 'build\u\ge007.u.map is absent'
    Add-Failure ("build\u\ge007.u.map is missing. gen_segments.py reads it to place`n" +
                 "    the ROM-flavoured symbols. It is produced by the matching build`n" +
                 "    (make matching), which needs the MIPS/IDO toolchain and is not`n" +
                 "    part of the Windows workflow.") ''
}

# ---------------------------------------------------------------- ROM ------

$rom = $env:SL_ROM
if (-not $rom) { $rom = Join-Path $repo 'baserom.u.z64' }
if (Test-Path $rom) {
    Write-Check $true 'ROM (for play.ps1)' $rom
} else {
    # Not a build requirement, so not a failure - only build.ps1 gates on the
    # list above. play.ps1 says the same thing at the point it matters.
    Write-Check $false 'ROM (for play.ps1)' "$rom - set SL_ROM to your own GoldenEye (U) copy"
}

# --------------------------------------------------------------- install ---

if ($script:Packages.Count -gt 0 -and $Install) {
    if ($null -eq $pacman) {
        Write-Host ''
        Write-Host "setup: -Install needs pacman, and there is none under $msysRoot."
    } else {
        Write-Host ''
        Write-Host "Installing with pacman: $($script:Packages -join ' ')"
        $go = $Yes
        if (-not $go) {
            $ans = Read-Host 'Proceed? [y/N]'
            $go = ($ans -eq 'y' -or $ans -eq 'Y')
        }
        if ($go) {
            & $pacman -S --needed --noconfirm @($script:Packages)
            if ($LASTEXITCODE -ne 0) {
                Write-Host "setup: pacman exited $LASTEXITCODE."
                Exit-Setup 1
            }
            Write-Host ''
            Write-Host 'Installed. Re-run .\tools\windows\setup.ps1 to re-validate.'
            Exit-Setup 0
        }
    }
}

# ----------------------------------------------------------- live probe ----
# Only reached when every static check passed. Compiling and linking for real
# is the only thing that proves the toolchain, and reading the PE header back
# is the only thing that proves what it produced.

if ($script:Failures.Count -eq 0 -and $CC) {
    $probeDir = Join-Path $repo 'build\win32\tmp'
    if (-not (Test-Path $probeDir)) { New-Item -ItemType Directory -Force -Path $probeDir | Out-Null }
    $probeC   = Join-Path $probeDir 'setup_probe.c'
    $probeExe = Join-Path $probeDir 'setup_probe.exe'
    $src = @'
#include <stdio.h>
#include <SDL.h>
int main(void) {
    SDL_version v;
    SDL_GetVersion(&v);
    printf("%d.%d.%d %d\n", v.major, v.minor, v.patch, (int)sizeof(void *));
    return 0;
}
'@
    [System.IO.File]::WriteAllText($probeC, ($src -replace "`r`n", "`n"),
                                   (New-Object System.Text.UTF8Encoding($false)))
    $cflags = @($sdlCflags -split '\s+' |
                Where-Object { $_ -ne '' -and $_ -ne '-Dmain=SDL_main' })
    $cflags += '-DSDL_MAIN_HANDLED'
    # Not $args - that is a PowerShell automatic variable and assigning to it
    # inside an advanced script does not do what it looks like.
    $probeArgs = @('-m32', '-no-pie', '-mconsole', '-Wl,--large-address-aware') +
                 $cflags + @($probeC, '-o', $probeExe, '-lmingw32', '-lSDL2', '-lopengl32')
    Remove-Item -Force $probeExe -ErrorAction SilentlyContinue
    & $CC @probeArgs
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $probeExe)) {
        Write-Check $false 'probe build' 'the toolchain could not compile and link an SDL2 program'
        Add-Failure 'A probe compile+link against SDL2 failed. See the compiler output above.' ''
    } else {
        $f = Get-PeFacts $probeExe
        $laa = (($f.Characteristics -band 0x0020) -ne 0)
        $ok  = ($f.Machine -eq 0x14c) -and ($f.Magic -eq 0x10b) -and $laa
        $detail = ("machine 0x{0:x4}, magic 0x{1:x3}, characteristics 0x{2:x} (LARGE_ADDRESS_AWARE {3})" -f
                   $f.Machine, $f.Magic, $f.Characteristics, $(if ($laa) { 'set' } else { 'CLEAR' }))
        Write-Check $ok 'probe build' $detail
        if (-not $ok) {
            Add-Failure 'The probe linked, but not as PE32 / i386 / LARGE_ADDRESS_AWARE.' ''
        }
        Remove-Item -Force $probeExe -ErrorAction SilentlyContinue
    }
    Remove-Item -Force $probeC -ErrorAction SilentlyContinue
}

# ---------------------------------------------------------------- verdict --

Write-Host ''
if ($script:Failures.Count -eq 0) {
    Write-Host 'Toolchain satisfied. Next:'
    Write-Host '    .\tools\windows\build.ps1      build sightline.exe'
    Write-Host '    .\tools\windows\test.ps1       headless Facility smoke test'
    Write-Host '    .\tools\windows\play.ps1       play Facility in a window'
    Write-Host ''
    Exit-Setup 0
}

Write-Host ("$($script:Failures.Count) requirement(s) not satisfied:")
foreach ($f in $script:Failures) { Write-Host "  - $f" }
if ($script:Packages.Count -gt 0) {
    Write-Host ''
    Write-Host 'Install them with:'
    Write-Host "    .\tools\windows\setup.ps1 -Install"
    Write-Host "  which runs:  pacman -S --needed $($script:Packages -join ' ')"
}
Write-Host ''
Exit-Setup 1
