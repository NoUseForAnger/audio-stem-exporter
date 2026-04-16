@echo off
echo Installing Audio Stem Exporter for OBS...
echo.

set SCRIPT_DIR=%~dp0
set DLL=%SCRIPT_DIR%build_x64\RelWithDebInfo\obs-mp3-writer.dll
set OBS_PLUGINS=%ProgramFiles%\obs-studio\obs-plugins\64bit
set OBS_DATA=%ProgramFiles%\obs-studio\data\obs-plugins\obs-mp3-writer\locale
set LOCALE=%SCRIPT_DIR%data\locale\en-US.ini

copy /Y "%DLL%" "%OBS_PLUGINS%\obs-mp3-writer.dll"

if errorlevel 1 (
    echo.
    echo ERROR: Copy failed. Make sure you right-clicked and chose "Run as administrator"
    echo and that OBS is fully closed.
) else (
    echo.
    if not exist "%OBS_DATA%" mkdir "%OBS_DATA%"
    copy /Y "%LOCALE%" "%OBS_DATA%\en-US.ini"
    echo SUCCESS - restart OBS to load the updated plugin.
)

echo.
pause
