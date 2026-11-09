@echo off
setlocal EnableExtensions
rem ============================================================
rem  ChroniconSkillMeter - push this project to GitHub
rem
rem  Works from ANY folder, and also when double-clicked.
rem
rem    "full\path\to\push_to_github.bat" https://github.com/USER/REPO.git
rem    push_to_github.bat --check          (environment check only, no network)
rem    double-click                       (asks for the URL)
rem ============================================================

title ChroniconSkillMeter - Push to GitHub

set "ROOT=%~dp0"
set "HERE=%~f0"
set "INTERACTIVE="
set "CHECKONLY="

cd /d "%ROOT%"
if errorlevel 1 (
    echo ERROR: cannot enter project folder:
    echo   %ROOT%
    goto :fail
)

echo.
echo ==========================================================
echo  ChroniconSkillMeter - push to GitHub
echo  Script : %HERE%
echo  Project: %ROOT%
echo ==========================================================
echo.

if /i "%~1"=="--check" set "CHECKONLY=1"
if /i "%~1"=="-c" set "CHECKONLY=1"

rem ---------- locate git.exe ----------------------------------
rem This machine may have no git on PATH (WorkBuddy ships a portable
rem copy). Check PATH first, then the usual install locations.
set "GITEXE="
for /f "delims=" %%I in ('where git 2^>nul') do if not defined GITEXE set "GITEXE=%%I"
if not defined GITEXE call :trygit "%ProgramFiles%\Git\cmd\git.exe"
if not defined GITEXE call :trygit "%ProgramFiles(x86)%\Git\cmd\git.exe"
if not defined GITEXE call :trygit "%LOCALAPPDATA%\Programs\Git\cmd\git.exe"
if not defined GITEXE call :trygit "%ProgramFiles%\WorkBuddyAI\resources\vendor\PortableGit\cmd\git.exe"

if not defined GITEXE (
    echo ERROR: git.exe was not found on this computer.
    echo.
    echo Install Git for Windows: https://git-scm.com/download/win
    echo Keep "Add Git to PATH" ticked, then run this script again.
    echo Or use the no-git upload fallback in README section 3.6.
    goto :fail
)

set "GITCMD="
for %%D in ("%GITEXE%") do set "GITCMD=%%~dpD"
for %%D in ("%GITCMD%..") do set "GITROOT=%%~fD"
rem Git Credential Manager lives in mingw64\bin. Without this, a push
rem that needs a browser sign-in fails with "credential-manager is not
rem a git command".
set "PATH=%GITROOT%\cmd;%GITROOT%\mingw64\bin;%PATH%"

echo Git    : %GITEXE%
echo GitRoot: %GITROOT%
"%GITEXE%" --version
echo.

rem ---------- verify local repository --------------------------
"%GITEXE%" rev-parse --is-inside-work-tree >nul 2>&1
if errorlevel 1 (
    echo ERROR: this folder is not a git repository:
    echo   %ROOT%
    goto :fail
)

set "BRANCH="
for /f "delims=" %%B in ('"%GITEXE%" branch --show-current 2^>nul') do set "BRANCH=%%B"
if not defined BRANCH set "BRANCH=main"

if not exist "%ROOT%.github\workflows\build.yml" (
    echo WARNING: .github\workflows\build.yml is missing.
    echo          Actions will not run after the push.
    echo.
)

if defined CHECKONLY goto :check

rem ---------- repository URL ----------------------------------
set "URL=%~1"
if defined URL goto :haveurl
set "INTERACTIVE=1"
echo Before you continue, create an EMPTY repository on github.com:
echo   1. Click the plus sign, then New repository
echo   2. Name it ChroniconSkillMeter, choose Public
echo   3. Do NOT tick Add a README file / .gitignore / license
echo   4. Copy the HTTPS .git URL shown on the next page
echo.
set /p "URL=Paste the .git URL here, then press Enter: "
:haveurl
set "URL=%URL: =%"
set "URL=%URL:"=%"
if not defined URL (
    echo ERROR: no repository URL was given.
    goto :fail
)

echo.
echo Repo   : %URL%
echo Branch : %BRANCH%
echo.
echo A GitHub sign-in window may open in your browser. Finish signing in.
echo GitHub does NOT accept your account password. If it asks for one,
echo paste a Personal Access Token with the "repo" scope instead.
echo.

"%GITEXE%" remote remove origin >nul 2>&1
"%GITEXE%" remote add origin "%URL%"
if errorlevel 1 (
    echo ERROR: could not set remote origin.
    goto :fail
)
"%GITEXE%" push -u origin %BRANCH%
if errorlevel 1 goto :pushfail

echo.
echo ==========================================================
echo  PUSH OK
echo ==========================================================
echo Next:
echo   1. Open the repo on github.com, click Actions, then Build DLL.
echo   2. When it turns green, scroll to Artifacts and download the zip.
echo   3. Extract ChroniconSkillMeter.dll into mods\aurie next to the game.
echo.
goto :done

:pushfail
echo.
echo Push failed. Common causes:
echo   1. Sign-in was cancelled. Run this script again and sign in.
echo   2. The repo was created WITH a README. Run this first:
echo          "%GITEXE%" pull --rebase origin %BRANCH%
echo      then run this script again.
echo   3. Wrong URL, no permission, or a network / TLS error: retry.
echo   4. No credentials at all: see README 3.6 for PAT or the
echo      no-git upload fallback.
goto :fail

:check
echo ----------------------------------------------------------
echo  Environment check
echo ----------------------------------------------------------
echo Current branch:
"%GITEXE%" branch --show-current
echo Remotes:
"%GITEXE%" remote -v
echo Latest commit:
"%GITEXE%" log -1 --oneline
echo Working tree changes:
"%GITEXE%" status --short
if exist "%ROOT%.github\workflows\build.yml" (
    echo Workflow file: OK  ^(.github\workflows\build.yml^)
) else (
    echo Workflow file: MISSING
)
echo.
echo Check finished. To push, run this script again with your repo URL.
echo.
goto :done

:fail
echo.
echo RESULT: FAILED
if defined INTERACTIVE (
    echo.
    pause
)
exit /b 1

:done
if defined INTERACTIVE (
    echo.
    pause
)
exit /b 0

:trygit
if exist "%~1" set "GITEXE=%~1"
exit /b 0
