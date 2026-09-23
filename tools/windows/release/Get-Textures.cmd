@echo off
rem Sightline - get the Community HD texture pack. Double-click this, once.
rem
rem It downloads the pinned official release of the Community HD project
rem straight from its maintainers (github.com/GhostlyDark/GoldenEye-007-HD) to
rem THIS machine, checks it against the size and SHA-256 recorded in
rem tools\community-source.json, and converts the textures into
rem %LOCALAPPDATA%\sightline\assets\texpacks\community, which is where the
rem game's TEXTURES = COMMUNITY HD setting reads them from.
rem
rem SIGHTLINE SHIPS NO TEXTURE AND MIRRORS NOTHING. This package carries no
rem pack bytes; the download is between you and the pack's own maintainers,
rem which is the arrangement they asked for. Nothing here is needed to play.
rem
rem It needs no Python, no repository and no build tools - only the Windows
rem PowerShell that ships with Windows - and re-running it is cheap: an
rem archive already downloaded and verified is used as it stands.
rem
rem The XBLA set is NOT obtained by this or any other tool: it is
rem user-supplied, and Sightline neither ships, locates nor downloads a source.
setlocal
cd /d "%~dp0"
if not exist "%~dp0tools\get-textures.ps1" (
    echo Get-Textures: tools\get-textures.ps1 is missing. Extract the whole ZIP.
    exit /b 1
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\get-textures.ps1" %*
set "RC=%ERRORLEVEL%"
echo.
pause
exit /b %RC%
