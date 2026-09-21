@echo off
rem Sightline - release launcher. The smallest thing that turns the extracted
rem folder into a playable game. It needs no PowerShell, no Python, no
rem repository and no installer; it writes only under %LOCALAPPDATA%\sightline.
rem
rem The ROM is YOURS and is never included: a plain, uncompressed, big-endian
rem .z64 dump of GoldenEye 007 (USA), 12582912 bytes. It is found, in order,
rem from SL_ROM if that names a file, else the first *.z64 beside this file,
rem else the path remembered from a previous run, else you are asked once.
setlocal
cd /d "%~dp0"
if not exist "%~dp0sightline.exe" (
    echo Sightline: sightline.exe is not beside this launcher. Extract the whole ZIP.
    exit /b 1
)

set "CFGDIR=%LOCALAPPDATA%\sightline"
if not defined LOCALAPPDATA set "CFGDIR=%USERPROFILE%\AppData\Local\sightline"

set "ROM="
if defined SL_ROM if exist "%SL_ROM%" set "ROM=%SL_ROM%"
if not defined ROM for %%F in ("%~dp0*.z64") do if not defined ROM set "ROM=%%~fF"
if not defined ROM if exist "%CFGDIR%\rom-path.txt" set /p ROM=<"%CFGDIR%\rom-path.txt"
if defined ROM if not exist "%ROM%" set "ROM="
if defined ROM goto :haverom

echo.
echo Sightline needs your own GoldenEye 007 (USA) ROM dump: a plain, uncompressed,
echo big-endian .z64 file (12582912 bytes, SHA-1 abe01e4aeb033b6c0836819f549c791b26cfde83).
echo The simplest setup is to copy it into this folder, beside Sightline.cmd.
echo .n64 / .v64 dumps are byte-swapped and will not work; convert or re-dump.
echo.
set /p "ROM=Path to your .z64 (or press Enter to quit): "
if not defined ROM exit /b 1
set "ROM=%ROM:"=%"
if not exist "%ROM%" (
    echo Sightline: no file at "%ROM%".
    exit /b 1
)
if not exist "%CFGDIR%" mkdir "%CFGDIR%"
>"%CFGDIR%\rom-path.txt" echo %ROM%

:haverom
for %%F in ("%ROM%") do set "ROMSIZE=%%~zF"
if not "%ROMSIZE%"=="12582912" (
    echo Sightline: "%ROM%" is %ROMSIZE% bytes; a GoldenEye 007 ^(USA^) .z64 dump is 12582912.
    echo See README.txt for the ROM requirement.
    exit /b 1
)
if not exist "%CFGDIR%" mkdir "%CFGDIR%"

set "SL_ROM=%ROM%"
set "SL_WINDOW=1"
if not defined SL_WINDOW_SIZE set "SL_WINDOW_SIZE=960x720"
set "SL_EEPROM_RW=%CFGDIR%\eeprom.bin"
set "SL_RUN=0"
"%~dp0sightline.exe"
exit /b %ERRORLEVEL%
