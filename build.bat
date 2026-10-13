@echo off
setlocal EnableExtensions

REM ===========================================================================
REM  ES40 Windows build script
REM
REM    es40.exe       SDL3 graphics + Rust "slirp" networking   (CMake)
REM    es40-cfg.exe   CLI configuration generator               (CMake)
REM    es40-cfg-tui   curses TUI configuration generator        (MSBuild)
REM
REM  Overridable environment variables:
REM    SDL3_DIR         folder containing SDL3Config.cmake.  If unset, SDL3 is
REM                     downloaded into SDL3_ROOT automatically.
REM    SDL3_ROOT        where to fetch SDL3 (default: <repo>\..\SDL3)
REM    ES40_BUILD_DIR   CMake build directory   (default: <repo>\build)
REM    ES40_VS_CONFIG   MSBuild configuration for the config tools
REM                     (default: "Release NN", which needs no Npcap SDK)
REM    ES40_BUILD_TUI   1/0, also build the TUI/CLI config tools (default: 1)
REM ===========================================================================

for %%i in ("%~dp0.")        do set "ROOT=%%~fi"
for %%i in ("%~dp0..\SDL3")  do set "SDL3_ROOT=%%~fi"
for %%i in ("%~dp0build")    do set "BUILD_DIR=%%~fi"
if defined ES40_BUILD_DIR set "BUILD_DIR=%ES40_BUILD_DIR%"
if not defined ES40_VS_CONFIG set "ES40_VS_CONFIG=Release NN"
if not defined ES40_BUILD_TUI set "ES40_BUILD_TUI=1"

echo ============================================================
echo  ES40 build
echo    repo:      %ROOT%
echo    build dir: %BUILD_DIR%
echo    SDL3 root: %SDL3_ROOT%
echo ============================================================
echo.

REM ---- locate Visual Studio and enter the x64 toolchain ----------------
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [ERROR] vswhere.exe not found - install Visual Studio with the C++ workload.
    goto :fail
)
set "VSDIR="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
    echo [ERROR] no Visual Studio C++ toolset found.
    goto :fail
)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
    echo [ERROR] vcvars64.bat failed.
    goto :fail
)
echo [ok] Visual Studio: %VSDIR%

REM ---- make cmake / ninja / cargo findable ------------------------------
set "VS_CMAKE=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
set "VS_NINJA=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
if exist "%VS_CMAKE%\cmake.exe" set "PATH=%VS_CMAKE%;%PATH%"
if exist "%VS_NINJA%\ninja.exe" set "PATH=%VS_NINJA%;%PATH%"
if exist "%USERPROFILE%\.cargo\bin\cargo.exe" set "PATH=%USERPROFILE%\.cargo\bin;%PATH%"

where cmake >nul 2>nul || ( echo [ERROR] cmake not found. & goto :fail )
where ninja >nul 2>nul || ( echo [ERROR] ninja not found. & goto :fail )
where cargo >nul 2>nul || ( echo [ERROR] cargo not found - install Rust from https://rustup.rs & goto :fail )

REM ---- ensure SDL3 is available -----------------------------------------
if not defined SDL3_DIR (
    call :ensure_sdl3
    if errorlevel 1 goto :fail
)
if not exist "%SDL3_DIR%\SDL3Config.cmake" (
    echo [ERROR] SDL3Config.cmake not found in "%SDL3_DIR%".
    goto :fail
)
echo [ok] SDL3: %SDL3_DIR%

REM ---- submodules needed by the TUI -------------------------------------
if "%ES40_BUILD_TUI%"=="1" (
    pushd "%ROOT%"
    git submodule update --init third_party/PDCursesMod third_party/netbsd-curses
    popd
)

REM ---- 1) CMake: es40 (graphics + slirp) and es40-cfg --------------------
echo.
echo [1/2] CMake: configure and build es40, es40-cfg
cmake -S "%ROOT%" -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSDL3_DIR="%SDL3_DIR%" -DES40_ENABLE_CFG_TUI=OFF
if errorlevel 1 goto :fail
cmake --build "%BUILD_DIR%" --target es40 es40-cfg -j
if errorlevel 1 goto :fail

for %%i in ("%SDL3_DIR%\..\lib\x64\SDL3.dll") do set "SDL3_DLL=%%~fi"
if exist "%SDL3_DLL%" (
    copy /y "%SDL3_DLL%" "%BUILD_DIR%\" >nul
    echo [ok] copied SDL3.dll next to es40.exe
) else (
    echo [warn] SDL3.dll not found - copy it next to es40.exe manually.
)

REM ---- 2) MSBuild: TUI and CLI config tools -----------------------------
if "%ES40_BUILD_TUI%"=="1" (
    echo.
    echo [2/2] MSBuild: es40-cfg-tui, es40-cfg
    call :build_msbuild
    if errorlevel 1 goto :fail
)

echo.
echo ============================================================
echo  Done.
echo    es40.exe      : %BUILD_DIR%\es40.exe
echo    es40-cfg.exe  : %BUILD_DIR%\es40-cfg.exe
if "%ES40_BUILD_TUI%"=="1" echo    es40-cfg-tui  : %ROOT%\src\Visual Studio\x64\%ES40_VS_CONFIG%\es40-cfg-tui.exe
echo ============================================================
endlocal
exit /b 0

:fail
echo.
echo BUILD FAILED.
endlocal
exit /b 1

REM ===========================================================================
REM  Subroutines
REM ===========================================================================

:ensure_sdl3
if exist "%SDL3_ROOT%\cmake\SDL3Config.cmake" (
    set "SDL3_DIR=%SDL3_ROOT%\cmake"
    echo [ok] using existing SDL3 at "%SDL3_ROOT%"
    exit /b 0
)
echo [info] downloading SDL3 into "%SDL3_ROOT%" ...
set "PS1=%TEMP%\es40_fetch_sdl3.ps1"
>  "%PS1%" echo $ErrorActionPreference = 'Stop'
>> "%PS1%" echo $ProgressPreference = 'SilentlyContinue'
>> "%PS1%" echo $root = '%SDL3_ROOT%'
>> "%PS1%" echo $tmp = Join-Path $env:TEMP 'es40-sdl3'
>> "%PS1%" echo Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
>> "%PS1%" echo New-Item -ItemType Directory -Force $tmp ^| Out-Null
>> "%PS1%" echo $loc = (^& curl.exe -fsSLI -o NUL -w '%%{url_effective}' 'https://github.com/libsdl-org/SDL/releases/latest')
>> "%PS1%" echo $tag = ([string]$loc).TrimEnd('/').Split('/')[-1]
>> "%PS1%" echo if (-not $tag) { throw 'could not resolve the latest SDL release' }
>> "%PS1%" echo $ver = $tag -replace 'release-',''
>> "%PS1%" echo $zip = Join-Path $tmp 'sdl3.zip'
>> "%PS1%" echo Invoke-WebRequest -UseBasicParsing "https://github.com/libsdl-org/SDL/releases/download/$tag/SDL3-devel-$ver-VC.zip" -OutFile $zip
>> "%PS1%" echo $ex = Join-Path $tmp 'ex'
>> "%PS1%" echo Expand-Archive -Path $zip -DestinationPath $ex -Force
>> "%PS1%" echo New-Item -ItemType Directory -Force $root ^| Out-Null
>> "%PS1%" echo Get-ChildItem -Path $ex -Directory ^| ForEach-Object { Copy-Item -Recurse -Force (Join-Path $_.FullName '*') $root }
>> "%PS1%" echo $cfg = Get-ChildItem -Path $root -Recurse -Filter SDL3Config.cmake ^| Select-Object -First 1
>> "%PS1%" echo if (-not $cfg) { throw 'SDL3Config.cmake not found after extraction' }
>> "%PS1%" echo $cfg.DirectoryName
set "SDL3_DIR="
for /f "usebackq delims=" %%i in (`powershell -NoProfile -ExecutionPolicy Bypass -File "%PS1%"`) do set "SDL3_DIR=%%i"
if not defined SDL3_DIR (
    echo [ERROR] SDL3 download failed.
    exit /b 1
)
echo [ok] SDL3: %SDL3_DIR%
exit /b 0

:build_msbuild
set "MSBUILD="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do set "MSBUILD=%%i"
if not defined MSBUILD (
    echo [warn] MSBuild.exe not found - skipping the config tools.
    exit /b 0
)
"%MSBUILD%" "%ROOT%\src\Visual Studio\es40-cfg-tui.vcxproj" /m /p:Configuration="%ES40_VS_CONFIG%" /p:Platform=x64 /v:m /nologo
if errorlevel 1 exit /b 1
"%MSBUILD%" "%ROOT%\src\Visual Studio\es40-cfg.vcxproj" /m /p:Configuration="%ES40_VS_CONFIG%" /p:Platform=x64 /v:m /nologo
if errorlevel 1 exit /b 1
exit /b 0
