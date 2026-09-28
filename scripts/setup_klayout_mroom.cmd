@echo off
setlocal

set "ROOT=%~dp0.."
if not "%~1"=="" (
  set "KLAYOUT=%~1"
) else (
  set "KLAYOUT=%ROOT%\klayout-src"
)

if defined COMMONDB_ROOT (
  set "COMMONDB=%COMMONDB_ROOT%"
) else if exist "%ROOT%\..\CommonDB" (
  set "COMMONDB=%ROOT%\..\CommonDB"
) else (
  echo COMMONDB_ROOT not set and ..\CommonDB not found
  exit /b 1
)

set "STREAMERS=%KLAYOUT%\src\plugins\streamers"
set "MROOM=%ROOT%\integrations\klayout\mroom"
set "LINK=%STREAMERS%\mroom"

if not exist "%KLAYOUT%\src" (
  echo KLayout not found at %KLAYOUT%
  echo Usage: %~nx0 [path\to\klayout-src]
  exit /b 1
)

if exist "%LINK%" (
  rmdir "%LINK%" 2>nul
  del "%LINK%" 2>nul
)
mklink /J "%LINK%" "%MROOM%"
if errorlevel 1 exit /b 1

(
  echo COMMONDB_ROOT = %COMMONDB:\=/%
  echo KLAYOUT_SRC = %KLAYOUT%/src
) > "%MROOM%\db_plugin\local.pri"

findstr /C:"SUBDIRS += mroom" "%STREAMERS%\streamers.pro" >nul 2>&1 || (
  echo.>> "%STREAMERS%\streamers.pro"
  echo # CommonDB ROOM streamer>> "%STREAMERS%\streamers.pro"
  echo SUBDIRS += mroom>> "%STREAMERS%\streamers.pro"
)

echo Linked %LINK% -^> %MROOM%
echo Next: cd %KLAYOUT% ^&^& build.bat -j 4
