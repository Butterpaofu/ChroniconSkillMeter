@echo off
setlocal
rem Push this project to a GitHub repository you created on github.com.
rem Usage: push_to_github.bat https://github.com/USERNAME/REPO.git

set "ROOT=%~dp0"
set "URL=%~1"
if "%URL%"=="" (
    echo.
    echo Usage:  push_to_github.bat https://github.com/USERNAME/REPO.git
    echo.
    echo First create an EMPTY repository on github.com, then copy its .git URL.
    echo Do NOT tick "Add a README file", .gitignore or license on that page.
    exit /b 1
)

cd /d "%ROOT%"
git rev-parse --is-inside-work-tree >nul 2>&1
if errorlevel 1 (
    echo ERROR: this folder is not a git repository.
    exit /b 1
)

echo.
echo Repo : %URL%
echo Local: %ROOT%
echo.
echo A GitHub sign-in window may open in your browser. Finish signing in.
echo.

git remote remove origin >nul 2>&1
git remote add origin "%URL%"
git push -u origin main
if errorlevel 1 (
    echo.
    echo Push failed. Common causes:
    echo   1. Sign-in was cancelled. Run this script again and sign in.
    echo   2. The repo was created WITH a README. Run the two lines below,
    echo      then run this script again:
    echo          git pull --rebase origin main
    echo   3. Wrong URL, no permission, or a network/TLS error - just retry.
    exit /b 1
)

echo.
echo PUSH OK
echo Next: open the repo on GitHub, click Actions, then Build DLL.
echo       When it finishes, scroll to Artifacts and download the zip.
echo.
exit /b 0
