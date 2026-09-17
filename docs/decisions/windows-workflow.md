# Windows: a PowerShell developer workflow, not a Bash-shaped one

**Decided:** 2026-09-02, following the owner's platform pivot to Windows-only
development.
**Status:** in force. `tools/windows/build.sh` is retired.

## The decision

Sightline's Windows workflow is three PowerShell entry points plus one gate,
run from an ordinary Windows PowerShell prompt at the repository root:

```
tools\windows\setup.ps1     provision and diagnose the toolchain
tools\windows\build.ps1     the authoritative Windows build
tools\windows\play.ps1      launch a level in a window
tools\windows\test.ps1      headless Facility acceptance
```

A developer needs none of: an MSYS2 Bash shell, `/c/...` MSYS paths,
`/dev/null`, POSIX `timeout`, `env VAR=value`, WSL, or any Linux-side
repository.

**MSYS2 stays, as a toolchain provider only.** `i686-w64-mingw32-gcc.exe` is a
legitimate native Windows compiler emitting PE32/i386, and there is no reason to
replace a working toolchain. What is retired is the requirement that ordinary
development be conducted *inside* MSYS2's shell. `build.ps1` and `setup.ps1`
each prepend MSYS2's `mingw32\bin` to PATH for their own child processes and
restore it on exit; the developer's shell is left as it was found.

Two MSYS2 executables are still invoked internally, deliberately:

- **`sed`**, to apply `tools/native/aiprint.sed` to `chraidata.c`. That
  transform mirrors the Makefile's `ConvertAIPRINT` and must stay in exactly one
  place; reimplementing it in PowerShell would create a second copy to drift.
- **`objdump`**, to read the PE import table so DLL staging is measured rather
  than guessed.

Everything else that `build.sh` shelled out to — `sort`, `comm`, `wc`,
`basename`, `ls -t` — is native PowerShell/.NET now, because those are
straightforward and because two of them were hiding silent hazards (below).

## Why not keep `build.sh`

It was explicitly a bring-up artefact. Keeping it would mean maintaining two
descriptions of one build, and the flags it carries are the kind where a
divergence is discovered months later as a wrong consumed value. It is retired
rather than kept "for Linux compatibility": `tools/native/build.sh` is the Linux
build and is untouched.

Tree-wide grep for `tools/windows/build.sh` (excluding `build/` and `.git/`)
returned only its own usage comment. No Makefile target, no `allbuild.sh`, no
document invoked it. `tools/windows/pe_asm.py` survives — `build.ps1` calls it
twice.

One point of history, so nobody hunts for a deletion commit: `tools/windows/`
was entirely untracked when this workflow was written. `build.sh` existed only
in the working tree and was never committed, so it leaves no trace in the log —
the directory enters history already holding `build.ps1` instead.

Its measured rationale is carried forward verbatim into `build.ps1`'s comments
and summarised in `tools/windows/README.md`. That was the point of retiring it
carefully rather than deleting it.

## What PowerShell 5.1 forced

The environment is Windows PowerShell 5.1, so the scripts avoid `&&`/`||`,
ternary, `??`, `?.` and `-AsHashtable`. Four things were measured rather than
assumed, and each fails in the direction of looking successful:

**1. Never `2>&1` on a native executable.** 5.1 wraps each stderr line in an
`ErrorRecord` and sets `$?` false even on exit 0. This build writes *all* of its
evidence to stderr, so a passing 300-frame run would have reported as a failure.
The harvest link redirects stderr through `System.Diagnostics.Process`;
`test.ps1` reads both pipes as separate async tasks.

**2. `Start-Process -PassThru` without `-Wait` never populates `.ExitCode`.**
PowerShell does not retain the process handle. Measured directly: a `cmd /c exit
7` read back an empty exit code, while the same command with `-Wait` read back 9
correctly. But `-Wait` offers no timeout, and a gate that can hang forever is not
a gate. `test.ps1` therefore constructs `System.Diagnostics.Process` itself,
which keeps the handle and allows `WaitForExit(ms)`. This was not theory — the
first two acceptance runs failed with `exit ` printed blank and the expected
line present.

**3. Set-difference must use ordinal comparison.** `gen_segments.py` sorts by
codepoint; `Sort-Object` is culture-aware. `build.ps1` uses a `HashSet` with
`[StringComparer]::Ordinal`, so collation cannot affect the result at all.

**4. Line endings must be handled explicitly.** `gen_segments.py` writes
`.covered` through `Path.write_text`, which on Windows Python emits CRLF. A
covered symbol compared with its CR attached matches nothing, and every
map-covered symbol then gets a no-op stub on top of its real definition — a wall
of multiple-definition errors naming neither cause. `build.ps1` strips CR
explicitly and writes its own files as UTF-8 without BOM, LF.

Hazards 3 and 4 existed in `build.sh` as `LC_ALL=C` and `tr -d '\015'`. Both are
*inert on a machine whose locale is already C*, which is how they read as
redundant to anyone porting them. They are load-bearing for a developer whose
`LANG` is set.

## PATH is not optional

MEASURED: `i686-w64-mingw32-gcc.exe` invoked by absolute path with `mingw32\bin`
absent from PATH **fails to build at all**. Its `as.exe` and `ld.exe` live under
`i686-w64-mingw32\bin` and load their runtime DLLs from `mingw32\bin`; without
it they die with `0xC0000135` (`STATUS_DLL_NOT_FOUND`) printing nothing
whatsoever, which reads exactly like a tool that ran and had nothing to say.
`setup.ps1` initially reported empty version strings for `as` and `ld` and
called them present — hence the rule that a version check must require both exit
0 *and* output.

## Runtime DLL staging

`build.ps1` copies the non-system imports next to the exe, reading the names out
of the PE import table rather than hardcoding them. This matters: the closure is
`SDL2.dll` **and `libwinpthread-1.dll`**, and the latter appears in no link line
and no pkgconf output, so a staging step written by reading the link command
copies `SDL2.dll` and stops.

Verified by launching with `C:\msys64\mingw32\bin` absent from the child's PATH
(`PATH=C:\Windows\system32;C:\Windows`): exit 0, `survived 300 pumped frames`.
A PATH-inheriting shell proves nothing here.

## Equivalence to the retired build

The PowerShell build was checked against the invariants captured from the
Bash-built artefacts, not merely checked for producing *a* binary:

| | Bash baseline | `build.ps1` |
|---|---|---|
| compiled objects / `*.o` files | 234 / 237 | 234 / 237 |
| stub count | 29 | 29 |
| md5 over the sorted stub symbol list | `f3dbfe843fdfbce316fd38069b475d03` | identical |
| machine / optional-header magic | `0x014c` / `0x010b` | same |
| `LARGE_ADDRESS_AWARE` | set | set |
| subsystem | 3 (CONSOLE) | same |
| sections | 16 | 16 |

A differing stub count or set would mean a compile class, a library, or the
filtering step had diverged. It did not.

Clean build 27s; no-op rebuild 1.6s; touching one `.c` rebuilds exactly that
object (plus the three link artefacts, which are regenerated every build by
design).

## Note on `Characteristics`

The exe reads `Characteristics 0x0126` —
`EXECUTABLE_IMAGE | LINE_NUMS_STRIPPED | LARGE_ADDRESS_AWARE | 32BIT_MACHINE`.
An earlier note recorded `0x00e0` for the same binary; that value lacks
`EXECUTABLE_IMAGE`, which every PE image sets, and is almost certainly a read at
the wrong offset. Verification should test the **bit** (`0x0020`), not the word,
which is what `setup.ps1` does.
