@echo off
setlocal enabledelayedexpansion
title SHANKPIT local NOCK
color 0B

REM local_nock.bat -- start the local NOCK map editor without the game.
REM
REM This is the same server the SHANKPIT APPS page's NOCK tile starts (bundled\dw_nock.exe, or
REM bin\local_nock_parena.exe in a dev tree), with two things the tile cannot give you: a real
REM console that stays open, and every line of output in var\logs\nock.log instead of swallowed.
REM
REM Founder real-time, 2026-10-31: "add a separate local nock bat start file or whatever and ensure
REM any logs are output into a text file not just swallowed when a screen opens and closes right
REM away". So: nothing here closes silently -- if the server dies, this window prints its last lines
REM from the log and waits.
REM
REM Usage: double-click, or `local_nock.bat [port]`.

REM ROOT must NOT end in a backslash. %~dp0 does, and `--root "C:\dir\"` hands the C runtime a
REM closing \" -- an ESCAPED quote -- so the path arrived mangled and the editor came up blank.
REM `for` with the trailing `.` resolves to a full path with no trailing separator.
for %%I in ("%~dp0.") do set "ROOT=%%~fI"
set "PORT=%~1"
if "%PORT%"=="" set "PORT=8787"
set "LOGDIR=%ROOT%var\logs"
set "LOG=%LOGDIR%\nock.log"
if not exist "%LOGDIR%" mkdir "%LOGDIR%" >nul 2>&1

echo. >> "%LOG%"
echo ==== local_nock.bat %DATE% %TIME% port=%PORT% ==== >> "%LOG%"

set "SERVER="
if exist "%ROOT%bundled\dw_nock.exe"     set "SERVER=%ROOT%bundled\dw_nock.exe"
if not defined SERVER if exist "%ROOT%bin\dw_nock.exe"          set "SERVER=%ROOT%bin\dw_nock.exe"
if not defined SERVER if exist "%ROOT%bin\local_nock_parena.exe" set "SERVER=%ROOT%bin\local_nock_parena.exe"

echo ==========================================
echo   SHANKPIT LOCAL NOCK  ^(map editor^)
echo ==========================================
echo   root : %ROOT%
echo   log  : %LOG%
echo.

if defined SERVER goto :run_native

REM No native server built/bundled: fall back to the Python one. Same wire contract, so the editor
REM and a live in-game EDIT MAP session behave identically.
where python >nul 2>&1
if errorlevel 1 goto :no_server
echo   server: python tools\local_nock.py (no native binary found)
echo [local-nock] python fallback >> "%LOG%"
start "SHANKPIT NOCK (python)" /min cmd /c "cd /d "%ROOT%" ^&^& python tools\local_nock.py --port %PORT% >> "%LOG%" 2>&1"
goto :opened

:run_native
echo   server: %SERVER%
echo [local-nock] server=%SERVER% >> "%LOG%"
start "SHANKPIT NOCK" /min cmd /c ""%SERVER%" --port %PORT% --root "%ROOT%" >> "%LOG%" 2>&1"

:opened
REM give the server a moment to bind before the browser asks for the page
timeout /t 2 /nobreak >nul
start "" "http://127.0.0.1:%PORT%/"

echo.
echo   editor: http://127.0.0.1:%PORT%/
echo   the server keeps running in its own minimized window; close that window to stop it.
echo   every request and every error is appended to:
echo     %LOG%
echo.
echo   If the editor does not load, open that file -- it now says why instead of closing.
echo.
pause
exit /b 0

:no_server
echo   [FAIL] no NOCK server found and Python is not installed.
echo.
echo   Looked for:
echo     %ROOT%bundled\dw_nock.exe
echo     %ROOT%bin\dw_nock.exe
echo     %ROOT%bin\local_nock_parena.exe
echo.
echo   Build one of them:
echo     make local-nock-win          (Windows binary, cross-compiled)
echo     make local-nock-parena       (native binary)
echo.
echo   Or install Python 3 and this script will use tools\local_nock.py.
echo [local-nock] FAILED: no server binary, no python >> "%LOG%"
pause
exit /b 1
