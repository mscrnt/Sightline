<#
.SYNOPSIS
    Win32 / i686 / PE32 build for Sightline. The authoritative Windows build.

.DESCRIPTION
    Run from an ordinary Windows PowerShell prompt at the repository root:

        .\tools\windows\build.ps1

    No MSYS2 shell, no Git Bash, no WSL. MSYS2 is used only as a TOOLCHAIN
    PROVIDER - this script locates its mingw32 compiler and puts it on PATH
    for its own child processes, and restores PATH on the way out.

    Objects live in build\win32\ so they can never mix with the ELF objects in
    build\native\ - the link globs *.o and would happily swallow both.

    Replaces tools/windows/build.sh (retired 2026-09-02). Every measured
    rationale from that script is carried forward in the comments below;
    none of these flags are tuning knobs.

.PARAMETER Clean
    Delete build\win32 before building.

.PARAMETER Msys2Root
    Where MSYS2 lives, if it is not in one of the usual places. The
    SL_MSYS2_ROOT environment variable does the same thing.

.NOTES
    Environment overrides, all optional:
      SL_MSYS2_ROOT    MSYS2 installation root
      SL_TRACE_DEFS    extra -D flags for the trace harness (invalidates objects)
      SL_SDL_CFLAGS    replace what pkgconf reports for SDL2 cflags
      SL_SDL_LIBS      replace the SDL2 link libraries
      SL_PYTHON        python interpreter (default: .venv\Scripts\python.exe)
      SL_LINKMAP       the link-map input for gen_segments.py (default: the
                       tracked tools\native\ge007.u.linkmap.txt)
#>
[CmdletBinding()]
param(
    [switch]$Clean,
    [switch]$Demo,
    [string]$Msys2Root
)

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$OUT  = Join-Path $repo 'build\win32'

# ---------------------------------------------------------------- helpers --

function Write-TextFileLf {
    param([string]$Path, [string]$Text)
    # Explicit encoding, explicit newlines. Set-Content would use the ANSI
    # codepage and CRLF; both matter downstream (see the .covered note below).
    [System.IO.File]::WriteAllText($Path, $Text, (New-Object System.Text.UTF8Encoding($false)))
}

function Invoke-Tool {
    param([string]$Exe, [string[]]$Arguments, [string]$What)
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$What failed (exit $LASTEXITCODE)`n  $Exe $($Arguments -join ' ')"
    }
}

function Get-QuotedArg {
    param([string]$Value)
    if ($Value -match '[\s"]') { return '"' + ($Value -replace '"', '\"') + '"' }
    return $Value
}

function Invoke-ToolCaptureStderr {
    # Windows PowerShell 5.1 turns a native command's stderr into ErrorRecords
    # when you use 2>&1, and falsifies $?. Go through Process directly instead.
    # Only stderr is redirected, so stdout keeps flowing to the console and
    # there is no second pipe to drain.
    param([string]$Exe, [string[]]$Arguments)
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName              = $Exe
    $psi.Arguments             = (($Arguments | ForEach-Object { Get-QuotedArg $_ }) -join ' ')
    $psi.UseShellExecute       = $false
    $psi.RedirectStandardError = $true
    $psi.WorkingDirectory      = $repo
    $p = [System.Diagnostics.Process]::Start($psi)
    $err = $p.StandardError.ReadToEnd()
    $p.WaitForExit()
    return New-Object PSObject -Property @{ ExitCode = $p.ExitCode; StdErr = $err }
}

function Find-Mingw32Bin {
    param([string]$Override)
    # Probe, never hardcode. A repository file must not carry one developer's
    # install path, so the candidate list is generic and SL_MSYS2_ROOT is the
    # documented escape hatch.
    $explicit = $Override
    if (-not $explicit) { $explicit = $env:SL_MSYS2_ROOT }

    $roots = New-Object System.Collections.Generic.List[string]
    if ($explicit) {
        # An explicitly named root is AUTHORITATIVE. Falling back to some other
        # installation would build with a toolchain the developer did not ask
        # for and never say so.
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

function Sort-Ordinal {
    param([string[]]$Items)
    $a = [string[]]$Items
    [Array]::Sort($a, [System.StringComparer]::Ordinal)
    return $a
}

# ------------------------------------------------------------- toolchain ---

$mingwBin = Find-Mingw32Bin -Override $Msys2Root
if ($null -eq $mingwBin) {
    Write-Error ("build.ps1: no i686-w64-mingw32-gcc.exe found.`n" +
                 "  Run .\tools\windows\setup.ps1 for a full diagnosis, or set`n" +
                 "  SL_MSYS2_ROOT to your MSYS2 installation root.")
    exit 1
}
$msysRoot = Split-Path -Parent (Split-Path -Parent $mingwBin)
$CC       = Join-Path $mingwBin 'i686-w64-mingw32-gcc.exe'

# MEASURED, never assumed: an x86_64 driver silently emits PE32+ and every
# 32-bit assumption in this tree would then be wrong.
$target = & $CC -dumpmachine
if ($LASTEXITCODE -ne 0) {
    Write-Error "build.ps1: '$CC -dumpmachine' failed."
    exit 1
}
if ($target -notlike 'i686-*') {
    Write-Error "build.ps1: $CC targets $target, not i686. Refusing."
    exit 1
}

$python = $env:SL_PYTHON
if (-not $python) { $python = Join-Path $repo '.venv\Scripts\python.exe' }
if (-not (Test-Path $python)) {
    $fallback = Get-Command python.exe -ErrorAction SilentlyContinue
    if ($null -eq $fallback) {
        Write-Error "build.ps1: no Python at '$python' and none on PATH. Run setup.ps1."
        exit 1
    }
    $python = $fallback.Source
}

# THE LINK MAP INPUT. gen_segments.py places the ROM-flavoured symbols and
# the segment windows from the matching build's linker map. That map is a
# product of `make matching` (MIPS / IDO), which the Windows workflow cannot
# run, so what this build reads is the TRACKED extraction of it -
# tools/native/ge007.u.linkmap.txt, symbol names and link addresses only,
# with its provenance (source map SHA-256, the commit `make matching` ran
# at, the ROM it reproduced) in its header. Regenerate / verify it with
# tools/native/extract_linkmap.py after a `make matching`; SL_LINKMAP can
# name a raw map or another extraction for that comparison, and nothing
# else. There is no hidden dependency on a local build\u\ge007.u.map.
$mapFile = $env:SL_LINKMAP
if (-not $mapFile) { $mapFile = Join-Path $repo 'tools\native\ge007.u.linkmap.txt' }
if (-not (Test-Path $mapFile)) {
    Write-Error ("build.ps1: missing $mapFile`n" +
                 "  gen_segments.py reads the tracked extraction of the matching`n" +
                 "  build's linker map (tools/native/extract_linkmap.py).")
    exit 1
}

if ($Clean -and (Test-Path $OUT)) { Remove-Item -Recurse -Force $OUT }
foreach ($d in @($OUT, (Join-Path $OUT 'tmp'))) {
    if (-not (Test-Path $d)) { New-Item -ItemType Directory -Force -Path $d | Out-Null }
}

$savedPath = $env:PATH
$savedTmp  = $env:TMP
$savedTemp = $env:TEMP
$savedTmpd = $env:TMPDIR
$savedCwd  = (Get-Location).Path
try {
    # PATH for this script's child processes only; restored in the finally.
    $env:PATH = "$mingwBin;$env:PATH"
    # MEASURED: gcc stages piped and stdin-fed input through a temp file, and
    # with TMP/TEMP unset it chose a directory it could not write:
    #   C:\WINDOWS\ccbo9vyj.s: Error: can't open ... for reading
    # which names neither the real cause nor the file being built. Keep
    # temporaries inside the build tree so the build never depends on the
    # ambient environment.
    $tmpDir = Join-Path $OUT 'tmp'
    $env:TMPDIR = $tmpDir
    $env:TMP    = $tmpDir
    $env:TEMP   = $tmpDir

    Set-Location $repo

    Write-Host "  toolchain: $CC ($target)"

    # NOTHING ROM-DERIVED IS GENERATED HERE. Until v0.2.0 this step ran
    # tools/native/gen_resample_tab.py, which compiled 528 bytes of the audio
    # microcode's data segment into sightline.exe from a locally extracted
    # copy; the executable derives those tables from the player's own ROM at
    # start-up instead (src/platform/sl_ucode.c), so the build needs no
    # extracted segment, no bin\ input and no build\native\ header.

    # ------------------------------------------------------------- flags ---

    $DEFS = @('-DVERSION_US', '-DLANG_US', '-DREFRESH_NTSC', '-DLEFTOVERDEBUG',
              '-DLEFTOVERSPECTRUM', '-DBUGFIX_R0', '-DBYTEMATCH', '-DTARGET_N64',
              '-D_LANGUAGE_C')

    # THE DEMO BUILD IDENTITY, and the ONLY thing that turns demo policy on.
    #
    # -Demo is a build-time switch, never a runtime one: the shipped demo must
    # behave the same however it is launched, and an environment variable the
    # owner could forget to set (or a player could unset) is not a policy. So
    # there is deliberately no SL_DEMO getenv anywhere - the whole identity is
    # this one -D, read by #if defined(SL_DEMO_BUILD) in the sources.
    #
    # Narrow ON PURPOSE. This is not a feature-flag framework: it gates exactly
    # the demo access policy (Multiplayer dimmed and unselectable, every
    # single-player mission available, every menu cheat unlocked but none
    # active, the demo's own save path) and nothing else. A normal build never
    # defines it and is byte-for-byte the build it was before this switch
    # existed.
    # Held SEPARATELY as well as folded into $DEFS, because $DEFS does not
    # reach every compile: src/platform and src/gfx have their own flag list
    # further down and never see it. The demo save path lives in
    # src/platform/sl_ultra_shim.c, so a define added only to $DEFS compiled
    # the front-end policy in and left the save path out - MEASURED, as a demo
    # build that correctly applied the front-end policy (then Facility-only)
    # but still wrote the owner's normal eeprom.bin. Both sites now take
    # $DEMO_DEFS.
    $DEMO_DEFS = @()
    if ($Demo) { $DEMO_DEFS = @('-DSL_DEMO_BUILD') }
    $DEFS += $DEMO_DEFS
    $INC  = @('-I.', '-Iinclude', '-Iinclude/PR', '-Isrc', '-Isrc/game',
              '-Isrc/inflate', '-Isrc/libultra')

    $TRACE_DEFS_RAW = $env:SL_TRACE_DEFS
    if ($null -eq $TRACE_DEFS_RAW) { $TRACE_DEFS_RAW = '' }
    $TRACE_DEFS = @($TRACE_DEFS_RAW -split '\s+' | Where-Object { $_ -ne '' })

    # pkgconf emits -Dmain=SDL_main on Windows, which rewrites the token main
    # in every translation unit it reaches. This tree cannot accept that:
    # sl_main.c would become int SDL_main(void) and collide with SDL_main.h's
    # int SDL_main(int, char**) - a hard error, measured. Strip it, declare
    # SDL_MAIN_HANDLED, and let sdl_init() call SDL_SetMainReady().
    # See docs/decisions/windows-sdl-main.md.
    if ($env:SL_SDL_CFLAGS) {
        $sdlCflagsRaw = $env:SL_SDL_CFLAGS
    } else {
        $pkgconf = $null
        foreach ($nm in @('pkgconf.exe', 'pkg-config.exe')) {
            $cand = Join-Path $mingwBin $nm
            if (Test-Path $cand) { $pkgconf = $cand; break }
        }
        if ($null -eq $pkgconf) {
            Write-Error "build.ps1: no pkgconf/pkg-config in $mingwBin. Run setup.ps1."
            exit 1
        }
        $sdlCflagsRaw = (& $pkgconf --cflags sdl2) -join ' '
        if ($LASTEXITCODE -ne 0) {
            Write-Error "build.ps1: pkgconf could not describe sdl2. Run setup.ps1."
            exit 1
        }
    }
    $SDL_CFLAGS = @($sdlCflagsRaw -split '\s+' |
                    Where-Object { $_ -ne '' -and $_ -ne '-Dmain=SDL_main' })
    $SDL_CFLAGS += '-DSDL_MAIN_HANDLED'

    # No -lSDL2main and no -mwindows: -mwindows detaches the console and
    # discards every stderr diagnostic this build exists to print.
    if ($env:SL_SDL_LIBS) {
        $SDL_LIBS = @($env:SL_SDL_LIBS -split '\s+' | Where-Object { $_ -ne '' })
    } else {
        $SDL_LIBS = @('-lmingw32', '-lSDL2', '-lopengl32')
    }

    # -mno-ms-bitfields and -fno-common are BOTH load-bearing on MinGW, and
    # both were measured against a consumed-output failure, not chosen for
    # tidiness. Full write-up: docs/backlog.md, B-061.
    #
    # -mno-ms-bitfields: MinGW defaults to -mms-bitfields, which Linux GCC does
    #   not. Under the MS rule a non-bitfield member cannot share the tail of a
    #   bitfield's allocation unit, so bondtypes.h's StandTile - u32 id:24 then
    #   u8 room - becomes room@4 mid@6 tail@8 sizeof 12, where the N64 layout
    #   is room@3 mid@4 tail@6 sizeof 8. stanDetermineEOF walks tiles by
    #   tileSizes[(tile->tail.half >> 0xc) & 0xf] and stops on
    #   *(s32 *)tile == 0; reading tail from the wrong offset yields a size
    #   index that never lands on the terminator, so loading Facility's stan
    #   hung forever (stan.c:3162).
    #
    # -fno-common: image.c declares ptr_texture_alloc_start / _end /
    #   ptr_next_available_space / ptr_last_entry_facemapping as four
    #   consecutive tentative definitions and then reinterprets that group as
    #   one struct texpool (image.c:2536, tex.c:809, initmttex.c:15) - the
    #   layout the original map confirms at 0x8008c720..0x8008c72c. With
    #   -fcommon they are COMMON symbols the linker may order as it likes, and
    #   MinGW ld interleaved g_TexCacheCount at start+4, so pool->end and
    #   g_TexCacheCount aliased: the texture-pool end pointer was read back as
    #   the cache count (measured 71209600) and texInflateZlib ran off
    #   g_TexCacheItems[150]. -fno-common emits them in declaration order and
    #   restores the original layout.
    # -O IS SPLIT, and the split is measured, not stylistic. GCC defaults to
    # -O0 when no -O is given, and no -O was given anywhere in this script, so
    # every object here - the decomp AND the native renderer - was built with
    # optimisation entirely disabled. That was the whole of the renderer
    # headroom problem: the Nintendo frame spent 87% of a 16.7 ms VI period
    # inside sl_gfx_frame_dl at ~14.6 us per triangle (~40k cycles), which is an
    # unoptimised build rather than slow hardware.
    #
    #   NATIVE (src/gfx, src/platform): -O2. This is where the measured cost
    #     is, and it is code this project wrote - ordinary well-defined C, no
    #     IDO heritage. Boots and passes Facility acceptance at -O2.
    #
    #   DECOMP (everything on $CFLAGS): -O0, deliberately. -O2 here builds
    #     clean and then dies before the first pumped frame:
    #       ACCESS VIOLATION addr 0x00000310, access EXECUTE
    #       eip 0x00000310  ra 0x01dd0009  - control transferred to a non-code
    #       address, and the stack holds no return address inside the image.
    #     Bisected by directory 2026-09-04 and confirmed in BOTH directions:
    #     decomp -O2 with native -O0 reproduces the identical signature, and
    #     native -O2 with decomp -O0 passes. -fno-strict-aliasing did NOT fix
    #     it, so it is not plain TBAA. The decomp is full of IDO-era type
    #     punning and permuter artifacts (the image.c texpool group described
    #     above is exactly that shape) and -O2 is where such latent UB stops
    #     being harmless. Not chased further - the decomp is not the measured
    #     bottleneck, so the cost/benefit does not justify it. Backlog item.
    #
    # -fno-strict-aliasing rides along with -O2 regardless: these translation
    # units share the decomp's game structs, and TBAA assumptions across that
    # boundary are not worth the risk for no measured gain.
    #
    # Deliberately NOT added: -ffast-math, -funsafe-math-optimizations, or any
    # other flag that relaxes IEEE semantics. Float behaviour must not move.
    # -fno-toplevel-reorder and -fno-common stay for the layout reasons above.
    $OPTFLAGS_NATIVE = @('-O2', '-fno-strict-aliasing')

    $CFLAGS = @('-m32', '-g', '-mno-ms-bitfields', '-std=gnu89',
                '-fno-toplevel-reorder', '-fms-extensions', '-fno-builtin',
                '-fno-common', '-w', '-msse2', '-mfpmath=sse')

    # --large-address-aware is LOAD-BEARING, not a tuning knob. Without it a
    # 32-bit process tops out at 0x7ffeffff and sl_pager_init cannot reserve
    # the kseg2 window at 0xC0000000 at all; with it the ceiling is 0xfffeffff
    # under WOW64 and the reservation returns the requested base. Measured both
    # ways. -mconsole for the reason in docs/decisions/windows-sdl-main.md.
    $LDFLAGS = @('-m32', '-no-pie', '-mconsole', '-Wl,--large-address-aware')

    # ------------------------------------------------------------ stamps ---

    # The stamp covers the COMPILE FLAGS as well as the trace defs. It used to
    # hold only $env:SL_TRACE_DEFS, which meant a flag change alone invalidated
    # nothing: objects are otherwise rebuilt on source/header mtime, and
    # editing this script does not touch either. Changing -O and getting a link
    # of stale -O0 objects back is a silent wrong answer of exactly the kind
    # this build keeps paying for, so the flags are part of the key.
    # Supersedes the old .trace_defs stamp; its absence reads as a changed key
    # and forces one full rebuild, which is the correct direction.
    # $DEFS joins the key for the reason the comment above gives. Without it a
    # -Demo build and a normal build share one object directory and differ only
    # by a -D, so switching between them relinked STALE objects compiled under
    # the other policy - a normal exe with the demo's unlocked mission list
    # and cheat menu, or a demo exe that still gated them, either way a silent
    # wrong answer.
    $stampKey = (@($TRACE_DEFS_RAW) + $DEFS + $DEMO_DEFS + $CFLAGS + $OPTFLAGS_NATIVE + $LDFLAGS) -join ' '

    $stamp = Join-Path $OUT '.build_key'
    $prev  = ''
    if (Test-Path $stamp) { $prev = [System.IO.File]::ReadAllText($stamp) }
    if ($prev -ne $stampKey) {
        Get-ChildItem -Path (Join-Path $OUT '*.o') -ErrorAction SilentlyContinue |
            Remove-Item -Force -ErrorAction SilentlyContinue
    }
    Write-TextFileLf $stamp $stampKey

    $hdrTime = [DateTime]::MinValue
    foreach ($pat in @('src\*.h', 'src\game\*.h', 'include\*.h', 'include\PR\*.h')) {
        $hs = Get-ChildItem -Path (Join-Path $repo $pat) -File -ErrorAction SilentlyContinue
        foreach ($h in $hs) {
            if ($h.LastWriteTimeUtc -gt $hdrTime) { $hdrTime = $h.LastWriteTimeUtc }
        }
    }

    # ----------------------------------------------------------- compile ---

    $dirs = @('src/game', 'src/native', 'src/gfx', 'src', 'src/inflate',
              'src/platform', 'src/libultra/gu', 'src/libultra/audio',
              'src/libultrare/audio')
    $keep = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    $n = 0
    $compiled = 0
    $sedExe = Join-Path $msysRoot 'usr\bin\sed.exe'

    foreach ($d in $dirs) {
        $dirFull = Join-Path $repo ($d -replace '/', '\')
        if (-not (Test-Path $dirFull)) { continue }
        $files = Get-ChildItem -Path (Join-Path $dirFull '*.c') -File | Sort-Object Name
        foreach ($f in $files) {
            # crash.c is excluded, as it is in the Linux native build.
            if ($f.Name -eq 'crash.c') { continue }
            $rel = "$d/$($f.Name)"
            $b   = ($rel -replace '/', '_') -replace '\.c$', ''
            $obj = Join-Path $OUT "$b.o"
            [void]$keep.Add("$b.o")
            $n++

            $need = $true
            if (Test-Path $obj) {
                $ot = (Get-Item $obj).LastWriteTimeUtc
                $need = ($f.LastWriteTimeUtc -gt $ot) -or ($hdrTime -gt $ot)
            }
            if (-not $need) { continue }
            $compiled++

            if ($f.Name -eq 'chraidata.c') {
                # Mirror of the Makefile's ConvertAIPRINT. The transform stays
                # sed's, in one place; only the plumbing is PowerShell. Written
                # to a file rather than piped, because PowerShell 5.1 re-encodes
                # what it pipes between native processes and the object must not
                # depend on that. -Isrc/game is already in $INC, so the quoted
                # include still resolves from the build tree.
                if (-not (Test-Path $sedExe)) {
                    throw "build.ps1: chraidata.c needs $sedExe (MSYS2 'sed' package)."
                }
                $pre = Join-Path $OUT 'tmp\chraidata.pre.c'
                $txt = & $sedExe -E -f 'tools/native/aiprint.sed' $rel
                if ($LASTEXITCODE -ne 0) { throw "build.ps1: sed failed on $rel" }
                Write-TextFileLf $pre (($txt -join "`n") + "`n")
                Invoke-Tool $CC ($CFLAGS + $DEFS + $INC + $TRACE_DEFS +
                                 @('-x', 'c', $pre, '-c', '-o', $obj)) "compile $rel"
            }
            elseif ($d -eq 'src/platform' -or $d -eq 'src/gfx') {
                # -mno-ms-bitfields here too: this class shares game structs
                # with the decomp class above, and one binary must not contain
                # two bitfield ABIs. SDL2's public headers declare no bitfields,
                # so nothing in the SDL interface is affected (checked across
                # mingw32/include/SDL2).
                # This is the site that carries $OPTFLAGS_NATIVE, and it is
                # the one that mattered: src/gfx does NOT use $CFLAGS, it has
                # its own flag list, and that list had no -O in it either - so
                # the DL interpreter, the measured 87% of the Nintendo frame,
                # was being built at -O0 through a code path that reading
                # $CFLAGS alone would never reveal.
                Invoke-Tool $CC (@('-m32', '-w', '-mno-ms-bitfields',
                                   '-Werror=implicit-function-declaration',
                                   '-msse2', '-mfpmath=sse',
                                   "-I$OUT") + $OPTFLAGS_NATIVE + $SDL_CFLAGS +
                                 $TRACE_DEFS + $DEMO_DEFS +
                                 @('-c', $rel, '-o', $obj)) "compile $rel"
            }
            else {
                Invoke-Tool $CC ($CFLAGS + $DEFS + $INC + $TRACE_DEFS +
                                 @('-c', $rel, '-o', $obj)) "compile $rel"
            }
        }
    }

    $pruned = 0
    $existing = Get-ChildItem -Path (Join-Path $OUT '*.o') -File -ErrorAction SilentlyContinue
    foreach ($o in $existing) {
        if (@('stubs.o', 'segments.o', 'modelhit_pool.o', 'sightline_res.o') -contains $o.Name) { continue }
        if (-not $keep.Contains($o.Name)) { Remove-Item -Force $o.FullName; $pruned++ }
    }
    if ($pruned -gt 0) { Write-Host "  pruned $pruned orphaned object(s)" }
    Write-Host "  objects: $n  (compiled $compiled)"

    # ----------------------------------------------------- link, two pass --

    foreach ($f in @('stubs.o', 'stubs.c', 'segments.o')) {
        Remove-Item -Force (Join-Path $OUT $f) -ErrorAction SilentlyContinue
    }

    # modelhit_pool.o must exist BEFORE the harvest link, not after: it defines
    # g_ModelHitEntries, and a harvest without it stubs that symbol out so the
    # real link then sees both the stub and the pool.
    Invoke-Tool $python @('tools/windows/pe_asm.py', 'tools/native/modelhit_pool.s',
                          "$OUT/modelhit_pool.s") 'pe_asm.py (modelhit_pool)'
    Invoke-Tool $CC @('-m32', '-c', "$OUT/modelhit_pool.s",
                      '-o', "$OUT/modelhit_pool.o") 'assemble modelhit_pool.s'

    # ------------------------------------------------------ exe icon ---
    # tools/windows/sightline.rc names docs/brand/sightline.ico as icon
    # resource 1. Compiled here so the *.o glob below carries it into both
    # links; the -Demo build shares the object directory and takes the same
    # object, and it is independent of every -D so the .build_key needs no
    # change (a key change drops every *.o, this one included, and it is
    # simply rebuilt). It is exempt from the orphan prune above by name.
    # MEASURED (windres 2.47): the icon path in the .rc resolves against the
    # .rc's own directory and against --include-dir, NOT against the cwd, so
    # both the .rc and the include dir are passed absolute.
    # SDL2 2.32.10 (MEASURED from its import table: EnumResourceNamesW +
    # LoadIconW, no ExtractIconEx) takes the exe's first RT_GROUP_ICON for the
    # window class, so no SDL_SetWindowIcon call is needed in src/.
    $windres = Join-Path $mingwBin 'windres.exe'
    if (-not (Test-Path $windres)) {
        throw ("build.ps1: no windres.exe in $mingwBin (MSYS2 'mingw-w64-i686-binutils').`n" +
               "  It compiles tools/windows/sightline.rc, the exe icon. Run setup.ps1.")
    }
    $rcDir = Join-Path $repo 'tools\windows'
    $rc    = Join-Path $rcDir 'sightline.rc'
    $ico   = Join-Path $repo 'docs\brand\sightline.ico'
    $resO  = Join-Path $OUT 'sightline_res.o'
    foreach ($p in @($rc, $ico)) {
        if (-not (Test-Path $p)) { throw "build.ps1: missing $p (exe icon)." }
    }
    $needRes = $true
    if (Test-Path $resO) {
        $rt = (Get-Item $resO).LastWriteTimeUtc
        $needRes = ((Get-Item $rc).LastWriteTimeUtc -gt $rt) -or
                   ((Get-Item $ico).LastWriteTimeUtc -gt $rt)
    }
    if ($needRes) {
        Invoke-Tool $windres @('-F', 'pe-i386', "--include-dir=$rcDir",
                               '-i', $rc, '-o', $resO) 'windres sightline.rc'
    }

    $objs = @(Get-ChildItem -Path (Join-Path $OUT '*.o') -File |
              ForEach-Object { $_.FullName })

    # The harvest link MUST pass the same libraries as the real one, or a
    # missing library is silently turned into a no-op stub instead of failing.
    # It is expected to fail; its stderr is the product.
    $harvest = Invoke-ToolCaptureStderr $CC ($LDFLAGS + $objs +
                   @('-o', "$OUT/link1.tmp", '-lm') + $SDL_LIBS)
    Write-TextFileLf (Join-Path $OUT 'link1.err') $harvest.StdErr

    # mingw ld prints undefined symbols WITHOUT the i686-PE leading underscore
    # (measured), so the Linux expression applies unchanged.
    $rx = "undefined reference to ``?'?([A-Za-z_][A-Za-z0-9_]*)'"
    $unres = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
    foreach ($m in [regex]::Matches($harvest.StdErr, $rx)) {
        [void]$unres.Add($m.Groups[1].Value)
    }
    if ($unres.Count -eq 0 -and $harvest.ExitCode -ne 0) {
        throw ("build.ps1: the harvest link failed but produced no undefined-symbol" +
               " lines. See build\win32\link1.err.")
    }

    $unresSorted = Sort-Ordinal @($unres)
    Write-TextFileLf (Join-Path $OUT 'unresolved.txt') (($unresSorted -join "`n") + "`n")

    Invoke-Tool $python @('tools/native/gen_segments.py', $mapFile,
                          "$OUT/unresolved.txt", "$OUT/segments.elf.s") 'gen_segments.py'
    Invoke-Tool $python @('tools/windows/pe_asm.py', "$OUT/segments.elf.s",
                          "$OUT/segments.s") 'pe_asm.py (segments)'
    Invoke-Tool $CC @('-m32', '-c', "$OUT/segments.s",
                      '-o', "$OUT/segments.o") 'assemble segments.s'

    # Two measured hazards, both silent, both handled explicitly here rather
    # than delegated to sort/comm.
    #
    #   ORDINAL comparison: gen_segments.py sorts by codepoint while MSYS2
    #   sort and comm use locale collation, and they disagree on mixed-case
    #   decomp symbols. This step uses a HashSet with StringComparer.Ordinal,
    #   so collation cannot affect the result at all.
    #
    #   CR STRIPPING: gen_segments.py writes .covered through Path.write_text,
    #   which on Windows Python turns LF into CRLF. A covered name compared
    #   with its CR still attached matches nothing, and every map-covered
    #   symbol then gets a no-op stub on top of its real definition - a wall of
    #   multiple-definition errors naming neither cause.
    $coveredPath = Join-Path $OUT 'segments.elf.s.covered'
    $coveredText = [System.IO.File]::ReadAllText($coveredPath)
    $covered = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
    foreach ($line in ($coveredText -split "`n")) {
        $s = $line.Trim(@("`r", ' ', "`t"))
        if ($s -ne '') { [void]$covered.Add($s) }
    }
    $unstubbed = Sort-Ordinal @($unresSorted | Where-Object { -not $covered.Contains($_) })
    Write-TextFileLf (Join-Path $OUT 'unresolved.txt') (($unstubbed -join "`n") + "`n")

    $sb = New-Object System.Text.StringBuilder
    [void]$sb.Append(@'
/* auto-generated: symbols the native link cannot yet resolve.
   LOUD, not silent. A stub that quietly returns 0 is indistinguishable from a
   correct answer at runtime, and that is the exact shape of the bug this port
   keeps paying for (a silent sprintf stub once blanked every formatted string
   in the game). Anything that still needs a real implementation announces
   itself the first time it is called instead of corrupting a result in
   silence. */
#include <stdio.h>

'@)
    foreach ($s in $unstubbed) {
        if ($s -eq '') { continue }
        [void]$sb.Append("long $s(void) { static int said; if (!said) { said = 1;`n")
        [void]$sb.Append("    fprintf(stderr, `"sightline native: UNIMPLEMENTED $s() called\n`"); }`n")
        [void]$sb.Append("  return 0; }`n")
    }
    Write-TextFileLf (Join-Path $OUT 'stubs.c') ($sb.ToString() -replace "`r`n", "`n")
    Invoke-Tool $CC @('-m32', '-w', '-c', "$OUT/stubs.c",
                      '-o', "$OUT/stubs.o") 'compile stubs.c'

    Remove-Item -Force (Join-Path $OUT 'link1.tmp') -ErrorAction SilentlyContinue

    $objs = @(Get-ChildItem -Path (Join-Path $OUT '*.o') -File |
              ForEach-Object { $_.FullName })
    $exe = Join-Path $OUT 'sightline.exe'
    Invoke-Tool $CC ($LDFLAGS + $objs + @('-o', $exe, '-lm') + $SDL_LIBS) 'link sightline.exe'

    # ------------------------------------------------- runtime DLL stage ---
    # A plain PowerShell launch has no MSYS2 directory on PATH, so the
    # non-system imports have to sit beside the exe. Which ones those are is
    # read out of the PE import table rather than guessed.
    $objdump = Join-Path $mingwBin 'objdump.exe'
    $staged = @()
    if (Test-Path $objdump) {
        $names = & $objdump -p $exe |
                 Select-String 'DLL Name:\s*(\S+)' |
                 ForEach-Object { $_.Matches[0].Groups[1].Value }
        foreach ($dll in $names) {
            $src = Join-Path $mingwBin $dll
            if (Test-Path $src) {
                $dst = Join-Path $OUT $dll
                $copy = $true
                if (Test-Path $dst) {
                    $copy = (Get-Item $src).LastWriteTimeUtc -gt (Get-Item $dst).LastWriteTimeUtc
                }
                if ($copy) { Copy-Item -Force $src $dst }
                $staged += $dll
            }
        }
    }
    if ($staged.Count -gt 0) { Write-Host "  staged: $($staged -join ', ')" }

    # -------------------------------------------- asset override stage ---
    # Committed asset-override SOURCE packages become .slmodel files here, so
    # a player who builds gets the models with no import step. The binary is a
    # BUILD OUTPUT: it lands in build\win32\data\asset-overrides, which is
    # the exe-adjacent repository candidate the runtime already searches
    # (aov_repo_dirs, src/native/sl_asset_override.c:209), and it is not
    # tracked. A tree with no source packages prints one line and carries on.
    Invoke-Tool $python @('tools/asset/build_repo_assets.py',
                          (Join-Path $OUT 'data\asset-overrides')) 'build_repo_assets.py'

    Write-Host "  stubs: $($unstubbed.Count)"
    Write-Host "  built: build\win32\sightline.exe"
}
finally {
    $env:PATH   = $savedPath
    $env:TMP    = $savedTmp
    $env:TEMP   = $savedTemp
    $env:TMPDIR = $savedTmpd
    Set-Location $savedCwd
}
