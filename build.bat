@echo off
setlocal
rem One-click build for ChroniconSkillMeter.
rem Usage:  build.bat          -> Release|x64
rem         build.bat Win32    -> Release|Win32 (32-bit Chronicon.exe)

set "ROOT=%~dp0"
set "PLATFORM=%~1"
if "%PLATFORM%"=="" set "PLATFORM=x64"

echo.
echo === ChroniconSkillMeter build - Release %PLATFORM% ===
echo.

echo [1/4] Checking dependencies...
set "NEEDFETCH="
for %%F in ("extern\YYToolkit\Shared.hpp" "extern\YYToolkit\Shared.cpp" "extern\Aurie\shared.hpp" "extern\FunctionWrapper\FunctionWrapper.hpp") do (
    if not exist "%ROOT%%%~F" set "NEEDFETCH=1"
)
if defined NEEDFETCH call "%ROOT%fetch_deps.bat" >nul 2>&1
for %%F in ("extern\YYToolkit\Shared.hpp" "extern\YYToolkit\Shared.cpp" "extern\Aurie\shared.hpp" "extern\FunctionWrapper\FunctionWrapper.hpp") do (
    if not exist "%ROOT%%%~F" goto :missing
)
echo       headers OK

echo [2/4] Locating MSBuild...
set "MSBUILD="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo ERROR: vswhere.exe not found.
    echo        Install Visual Studio 2022 with the "Desktop development with C++" workload.
    echo        No Visual Studio? Use .github\workflows\build.yml to build in the cloud.
    exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do set "MSBUILD=%%i"
if not defined MSBUILD (
    echo ERROR: MSBuild.exe not found. Install the "Desktop development with C++" workload.
    exit /b 1
)
echo       %MSBUILD%

echo [3/4] Building...
"%MSBUILD%" "%ROOT%ChroniconSkillMeter.sln" /m /nologo /v:minimal /p:Configuration=Release /p:Platform=%PLATFORM%
if errorlevel 1 (
    echo ERROR: compilation failed. See the messages above.
    exit /b 1
)

set "DLL=%ROOT%bin\%PLATFORM%\Release\ChroniconSkillMeter.dll"
if not exist "%DLL%" (
    echo ERROR: build succeeded but the DLL was not found at:
    echo        %DLL%
    exit /b 1
)

echo [4/4] Copying to dist...
if not exist "%ROOT%dist" mkdir "%ROOT%dist"
copy /y "%DLL%" "%ROOT%dist\ChroniconSkillMeter.dll" >nul

echo.
echo   BUILD OK
echo   DLL ready at: %ROOT%dist\ChroniconSkillMeter.dll
echo.
echo   Next: put that file in ^<Chronicon game folder^>\mods\aurie\
echo         or use AurieManager's "Add Mods" button, see README section 3
echo.
exit /b 0

:missing
echo ERROR: a required header is missing from extern\
echo        Run fetch_deps.bat manually, or check your network connection.
exit /b 1
