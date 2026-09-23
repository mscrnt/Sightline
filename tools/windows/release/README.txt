Sightline {{VERSION}} - Windows (32-bit, OpenGL)
==================================================

Sightline is a natively compiled engine fork of the GoldenEye 007 N64
decompilation: the full game as a Windows executable, no emulator.

This package contains NO GoldenEye ROM and NO game data. Everything the game
draws, plays and loads is read at run time from YOUR OWN ROM dump.

What you need
-------------
  * Windows 10 or 11 (x64; this is a 32-bit build) with OpenGL drivers.
  * Your own GoldenEye 007 (USA) ROM dump as a plain, uncompressed,
    big-endian .z64 file:
        size   12582912 bytes
        SHA-1  abe01e4aeb033b6c0836819f549c791b26cfde83
    If your dump came as an archive, extract it first; Sightline does not
    read archives. Windows Explorer hides known extensions, so check the real
    file name in PowerShell (Get-ChildItem) - a rename can silently produce
    "name.z64.z64". A .n64 or .v64 file is byte-swapped and will not run
    correctly; convert it to .z64 with a ROM tool or re-dump.
    Self-check in PowerShell:
        (Get-Item .\your.z64).Length                    # 12582912
        (Get-FileHash .\your.z64 -Algorithm SHA1).Hash  # ABE01E4A...

Quick start
-----------
  1. Extract the whole ZIP to a folder of your choice.
  2. Copy your .z64 into that folder, beside Sightline.cmd.
     (Or set the SL_ROM environment variable to its full path; or let
     Sightline.cmd ask you for the path once - it remembers the answer in
     %LOCALAPPDATA%\sightline\rom-path.txt.)
  3. Double-click Sightline.cmd.

Sightline.cmd opens a 960x720 window (SL_WINDOW_SIZE overrides the initial
size) and runs sightline.exe from this folder. Running sightline.exe directly
is not the supported path: without the launcher's environment it opens no
window and finds no ROM.

Optional: the Community HD textures
-----------------------------------
Double-click Get-Textures.cmd, once. It downloads the pinned official release
of the Community HD project straight from its maintainers
(https://github.com/GhostlyDark/GoldenEye-007-HD), checks it against the size
and SHA-256 recorded in tools\community-source.json, and converts it into
%LOCALAPPDATA%\sightline\assets\texpacks\community. Then pick OPTIONS >
SETTINGS > DISPLAY > TEXTURES = COMMUNITY HD in the game (or the watch's
SIGHTLINE > GRAPHICS page, in a mission). It needs nothing but the Windows
PowerShell that ships with Windows, and re-running it costs one hash check.

This package contains no texture and mirrors nothing: that download is
between you and the pack's own maintainers. Sightline redistributes no part
of it. The XBLA set is user-supplied and no tool here obtains it.

Quitting
--------
  {{QUIT}}

Where things are written
------------------------
  %LOCALAPPDATA%\sightline\eeprom.bin     the cartridge save (created on first run)
  %LOCALAPPDATA%\sightline\rom-path.txt   the ROM path, only if the launcher asked for it
  %LOCALAPPDATA%\sightline\config.ini     your settings
  %LOCALAPPDATA%\sightline\assets\texpacks\community\  the converted textures,
                                          only if you ran Get-Textures.cmd
  %LOCALAPPDATA%\sightline\cache\         the downloaded pack archive, kept so
                                          a second run needs no network
Nothing inside the extracted folder is written to.

Verifying the download
----------------------
Beside the ZIP on the release page is Sightline-v{{VERSION}}-win32.zip.sha256,
one line: "<sha256>  <file name>". Compare it with the file's hash:
    PowerShell:  (Get-FileHash .\Sightline-v{{VERSION}}-win32.zip -Algorithm SHA256).Hash
    elsewhere:   sha256sum -c Sightline-v{{VERSION}}-win32.zip.sha256

Provenance
----------
VERSION.txt records the exact source commit this build came from, the
toolchain, and the SHA-256 of every file in this package. The public source
mirror is https://github.com/mscrnt/Sightline (Releases, Issues, wiki).

Licensing
---------
See LICENSES\README.md. Sightline-authored code is Zero-Clause BSD and
Sightline-authored artwork (the boot-screen models under data\asset-overrides)
is CC0 1.0. Any third-party material that ships carries its own notice under
LICENSES\. GoldenEye 007 is the property of its rightsholders; this project is
unaffiliated with Rare, Nintendo, MGM or Danjaq and distributes no ROM or
ROM-derived game data.
