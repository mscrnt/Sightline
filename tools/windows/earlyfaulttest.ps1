<#
.SYNOPSIS
    Build and run the offline tests for the opt-in early first-chance fault
    observer (src/platform/sl_early_fault.c), plus the structural source scan
    that no running test can replace.

.DESCRIPTION
    Run from an ordinary Windows PowerShell prompt at the repository root:

        .\tools\windows\earlyfaulttest.ps1

    WHAT IT RUNS. tools/profiling/native/test_sl_early_fault.c, which #includes
    the module's own text and drives it against a recording fake of the five
    Win32 calls the module takes by pointer. The real <windows.h> supplies
    EXCEPTION_RECORD, CONTEXT, EXCEPTION_POINTERS and the disposition constants,
    so the handler driven below has the real CALLBACK signature and the forged
    records have the real layout - but NOTHING IS REGISTERED WITH WINDOWS, no
    exception is ever raised, and the handler is called as an ordinary function.

    Nine modes, each its own process because the event cap and the armed state
    have process lifetime by construction:

        parse      the selector's grammar, including 24 malformed forms
        filter     the two-bit admission rule over 15 codes, and that the
                   handler obeys it: no line at all for an excluded code
        arm        silence when unset or off, a malformed value refused without
                   being echoed, the module's own observer registered once, and
                   a failed registration refusing with nothing left to remove
        observe    what one event records, and the two failure shapes inside it
        cap        the bound, exactly one suppressed line, counts still kept
        label      every line says FIRST-CHANCE and not-necessarily-terminal,
                   none imitates the product crash header
        lifecycle  add then remove then the product handler, idempotence, and a
                   failed removal keeping a working pass-through observer
        default    the path a launch that did not ask for this takes
        forged     NULL pointer sets, a NULL context, absurd parameter counts,
                   and that CONTINUE_EXECUTION is unreachable

    AND THE SOURCE SCAN, which is not a runtime property. Two of this
    diagnostic's safety claims cannot be demonstrated by running it:

      - NO LOADER CALL. GetModuleHandle and friends take the loader lock
        ("Dynamic-Link Library Best Practices", Microsoft Win32 docs). This
        observer is armed across GL initialisation, which LOADS the driver's
        ICD, so a first-chance exception raised while a thread holds the loader
        lock, met by a handler that asks the loader a question, is a deadlock -
        and a deadlock here would convert a crash into a hang and destroy the
        evidence. The same rule is enforced for the product's handler by
        tools/windows/crashreport-grammar.py check A5.
      - NO PAGER CALL. src/platform/sl_ultra_shim.c's sl_pager_fault calls
        sl_rom_load() for any address inside the kseg2 window - file I/O and
        allocation - and sl_pager_init runs LATER than even the product handler,
        so the window those addresses belong to is not yet reserved.

    Both are checked by reading the module's source for the forbidden names,
    with comments stripped first so a comment explaining why a call is absent
    cannot read as the call. Also checked: no <stdio.h>, <stdlib.h>, <string.h>
    or allocation on this path, and that the handler's only return is
    EXCEPTION_CONTINUE_SEARCH.

    Exits 0 when the scan and every mode pass, 1 otherwise.

.PARAMETER Msys2Root
    Where MSYS2 lives, if it is not in one of the usual places. The
    SL_MSYS2_ROOT environment variable does the same thing.

.PARAMETER OutDir
    Where the object and the executable go. Defaults to build\win32 beside the
    other fixtures; point it elsewhere to keep a run out of the build tree.
#>
[CmdletBinding()]
param([string]$Msys2Root, [string]$OutDir)

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if ($OutDir) { $OUT = $OutDir } else { $OUT = Join-Path $repo 'build\win32' }

if (-not $Msys2Root) { $Msys2Root = $env:SL_MSYS2_ROOT }
$roots = @()
if ($Msys2Root) { $roots += $Msys2Root }
$roots += @('C:\msys64', 'C:\msys2', "$env:SystemDrive\msys64")
$mingwBin = $null
foreach ($r in $roots) {
    $cand = Join-Path $r 'mingw32\bin'
    if (Test-Path (Join-Path $cand 'i686-w64-mingw32-gcc.exe')) { $mingwBin = $cand; break }
}
if ($null -eq $mingwBin) {
    Write-Error "earlyfaulttest.ps1: no mingw32 toolchain found. Run tools\windows\setup.ps1."
}
$CC = Join-Path $mingwBin 'i686-w64-mingw32-gcc.exe'

New-Item -ItemType Directory -Force -Path $OUT | Out-Null
$exe = Join-Path $OUT 'earlyfaulttest.exe'

$rc = 0

# ------------------------------------------------------------- source scan ---
# Comments are stripped first: this file documents at length WHY it never calls
# GetModuleHandleEx or sl_pager_fault, and a scan that counted those sentences
# would fail on the explanation rather than on a call.
$modPath = Join-Path $repo 'src\platform\sl_early_fault.c'
$src = [System.IO.File]::ReadAllText($modPath)
$noComments = [regex]::Replace($src, '/\*.*?\*/', '', 'Singleline')
$noComments = [regex]::Replace($noComments, '//[^\r\n]*', '')

# TWO LISTS, because the Win32 names carry DECORATION and the libc ones do not.
#
# A loader API appears as GetModuleHandleA, GetModuleHandleW, GetModuleHandleExA
# and so on, so these are matched with a trailing \w* - an exact-name pattern
# anchored at '(' silently misses every decorated spelling. That is not
# hypothetical: this scan was first written with exact names and a mutant that
# inserted GetModuleHandleA walked straight through it, which is the whole
# reason the mutant exists. tools/windows/crashreport-grammar.py enumerates the
# A and W variants by hand for the same reason; a prefix cannot be forgotten.
$loaderPrefixes = @(
    'GetModuleHandle', 'GetModuleFileName', 'GetModuleBaseName', 'LoadLibrary',
    'GetProcAddress', 'FreeLibrary', 'EnumProcessModules', 'SymInitialize',
    'SymFromAddr', 'SymGetModuleInfo', 'GetEnvironmentVariable'
)
# Exact names: the pager, the allocator and the library routines that take locks
# or allocate. A handler that allocates can fail while reporting a failure.
$exactNames = @(
    'sl_pager_fault', 'sl_pager_init', 'sl_rom_load',
    'malloc', 'calloc', 'realloc', 'free', 'strdup', 'getenv',
    'fopen', 'fprintf', 'printf', 'sprintf', 'snprintf', 'vsnprintf',
    'memcpy', 'memset', 'strlen', 'strcpy', 'strncpy', 'strcat'
)
$hits = @()
foreach ($name in $loaderPrefixes) {
    if ([regex]::IsMatch($noComments, '\b' + [regex]::Escape($name) + '\w*\s*\(')) {
        $hits += ($name + '*')
    }
}
foreach ($name in $exactNames) {
    if ([regex]::IsMatch($noComments, '\b' + [regex]::Escape($name) + '\s*\(')) {
        $hits += $name
    }
}
$forbidden = $loaderPrefixes + $exactNames
foreach ($inc in @('stdio.h', 'stdlib.h', 'string.h')) {
    if ([regex]::IsMatch($noComments, '#\s*include\s*<' + [regex]::Escape($inc) + '>')) {
        $hits += "#include <$inc>"
    }
}
if ($hits.Count -gt 0) {
    Write-Host ("FAIL  scan        sl_early_fault.c reaches {0} forbidden name(s): {1}" -f $hits.Count, ($hits -join ', '))
    $rc = 1
} else {
    Write-Host ("PASS  scan        no loader API, no pager call, no allocation, no stdio/string library call, and none of three libc headers in {0} forbidden names" -f $forbidden.Count)
}

# The handler's only disposition. EXCEPTION_CONTINUE_EXECUTION anywhere in this
# file would make the observer a handler, which is the one thing it may not be.
if ([regex]::IsMatch($noComments, 'EXCEPTION_CONTINUE_EXECUTION')) {
    Write-Host 'FAIL  scan        sl_early_fault.c names EXCEPTION_CONTINUE_EXECUTION'
    $rc = 1
} else {
    $n = ([regex]::Matches($noComments, 'return\s+EXCEPTION_CONTINUE_SEARCH')).Count
    if ($n -lt 3) {
        Write-Host "FAIL  scan        only $n CONTINUE_SEARCH return(s); the handler has more exits than that"
        $rc = 1
    } else {
        Write-Host "PASS  scan        $n returns of EXCEPTION_CONTINUE_SEARCH and no mention of CONTINUE_EXECUTION"
    }
}

# And the delta in sl_main.c is the two calls and the literal-free sink, nothing
# else: a diagnostic MESSAGE that drifted into sl_main.c would enter the reach of
# the sl_say-literal extraction in crashreport-grammar.py, which measures the
# reporter's unresolved-reason set from that file.
#
# The selector's NAME is expected there - it is the getenv argument, and getenv
# is the caller's job precisely so the handler never reads the environment - so
# the scan looks for this diagnostic's message TEXT instead: the FIRST-CHANCE
# label and the "early-fault:" line prefix, neither of which has any business in
# sl_main.c.
$mainSrc = [System.IO.File]::ReadAllText((Join-Path $repo 'src\platform\sl_main.c'))
$mainNoC = [regex]::Replace($mainSrc, '/\*.*?\*/', '', 'Singleline')
$leaks = @()
foreach ($text in @('FIRST-CHANCE', 'early-fault:', 'early first-chance')) {
    if ([regex]::IsMatch($mainNoC, '"[^"]*' + [regex]::Escape($text))) { $leaks += $text }
}
if ($leaks.Count -gt 0) {
    Write-Host ("FAIL  scan        diagnostic message text has drifted into sl_main.c literals: {0}" -f ($leaks -join ', '))
    $rc = 1
} else {
    Write-Host 'PASS  scan        sl_main.c carries no message literal of this diagnostic (the selector name in getenv is expected and is not one)'
}

# ------------------------------------------------------------------ build ---
# -m32 like the rest of the native build. -Wall -Wextra -Werror is stricter than
# the product's src/platform flags, which carry -w.
$flags = @('-m32', '-O2', '-Wall', '-Wextra', '-Werror',
           '-Werror=implicit-function-declaration',
           '-I' + (Join-Path $repo 'src\platform'))
$sources = @('tools/profiling/native/test_sl_early_fault.c')

$modes = @('parse', 'filter', 'arm', 'observe', 'cap', 'label', 'lifecycle',
           'default', 'forged')

$savedPath = $env:PATH
$savedCwd  = (Get-Location).Path
try {
    # cc1 and the runtime DLLs live beside gcc; without this the driver fails
    # with a missing-DLL error that reads like a missing compiler.
    $env:PATH = "$mingwBin;$env:PATH"
    Set-Location $repo
    & $CC @($flags + @('-o', $exe) + $sources)
    if ($LASTEXITCODE -ne 0) { throw "earlyfaulttest.ps1: compile failed (exit $LASTEXITCODE)" }

    foreach ($mode in $modes) {
        & $exe $mode
        if ($LASTEXITCODE -ne 0) {
            Write-Host "FAIL: mode $mode exited $LASTEXITCODE"
            $rc = 1
        }
    }
} finally {
    Set-Location $savedCwd
    $env:PATH = $savedPath
}
if ($rc -eq 0) { Write-Host 'earlyfaulttest: the source scan and all modes passed' }
exit $rc
