@echo off
setlocal

set "PROJECT_DIR=%~dp0"
if "%PROJECT_DIR:~-1%"=="\" set "PROJECT_DIR=%PROJECT_DIR:~0,-1%"
set "BUILD_DIR=%PROJECT_DIR%\build"

if /I "%~1"=="build" goto build
if /I "%~1"=="run" goto run
if /I "%~1"=="debug" goto debug
if /I "%~1"=="offline" goto offline
if /I "%~1"=="offline" goto offline
if /I "%~1"=="help" goto usage
if "%~1"=="" goto usage

echo Unknown command: %~1
goto usage_error

:build
call :configure
if errorlevel 1 exit /b %errorlevel%

cmake --build "%BUILD_DIR%" --config Debug
exit /b %errorlevel%

:run
call :build
if errorlevel 1 exit /b %errorlevel%

call :find_app
if errorlevel 1 exit /b 1

start "" /D "%PROJECT_DIR%" "%APP%"
exit /b %errorlevel%

:debug
rem Builds, then runs the app in this console with the debug harness on: API requests, file
rem operations, child processes and app.log lines print here. Channels: http, body, file, process,
rem log, all (default; body is not part of all). Example: mcdebug debug http,file
call :build
if errorlevel 1 exit /b %errorlevel%
call :find_app
if errorlevel 1 exit /b 1

set "CHANNELS="
:collect_channels
shift
if "%~1"=="" goto run_debug
set "CHANNELS=%CHANNELS%,%~1"
goto collect_channels

:run_debug
if defined CHANNELS (set "MCLP_DEBUG=%CHANNELS:~1%") else set "MCLP_DEBUG=all"
echo Debug harness on [%MCLP_DEBUG%]. Close the app window or press Ctrl+C to stop.
pushd "%PROJECT_DIR%"
"%APP%"
set "APP_EXIT=%errorlevel%"
popd
exit /b %APP_EXIT%


:offline
rem Builds, then runs the app in this console with every network request refused at once, so a
rem launch that doesn't need the pack APIs, images or sign-in doesn't call them. Blocked requests
rem show in the http trace. Channels work as in debug (default: http,log). Example: mcdebug offline file
call :build
if errorlevel 1 exit /b %errorlevel%
call :find_app
if errorlevel 1 exit /b 1

set "CHANNELS="
:collect_offline_channels
shift
if "%~1"=="" goto run_offline
set "CHANNELS=%CHANNELS%,%~1"
goto collect_offline_channels

:run_offline
if defined CHANNELS (set "MCLP_DEBUG=%CHANNELS:~1%") else set "MCLP_DEBUG=http,log"
set "MCLP_OFFLINE=1"
echo Offline mode on, debug harness on [%MCLP_DEBUG%]. Close the app window or press Ctrl+C to stop.
pushd "%PROJECT_DIR%"
"%APP%"
set "APP_EXIT=%errorlevel%"
popd
exit /b %APP_EXIT%

:find_app
if exist "%BUILD_DIR%\Debug\MC_Launcher_Premium.exe" (
    set "APP=%BUILD_DIR%\Debug\MC_Launcher_Premium.exe"
    exit /b 0
)
if exist "%BUILD_DIR%\MC_Launcher_Premium.exe" (
    set "APP=%BUILD_DIR%\MC_Launcher_Premium.exe"
    exit /b 0
)
echo Could not find the built MC_Launcher_Premium.exe.
exit /b 1

:configure
call :find_cmake
if errorlevel 1 exit /b 1

if defined VCPKG_ROOT (
    if not exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" (
        echo VCPKG_ROOT does not contain scripts\buildsystems\vcpkg.cmake: "%VCPKG_ROOT%"
        exit /b 1
    )
    cmake -S "%PROJECT_DIR%" -B "%BUILD_DIR%" -DCMAKE_BUILD_TYPE=Debug "-DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
) else (
    cmake -S "%PROJECT_DIR%" -B "%BUILD_DIR%" -DCMAKE_BUILD_TYPE=Debug
)
exit /b %errorlevel%

:find_cmake
rem Use cmake from PATH if present; otherwise fall back to the copy bundled with Visual Studio.
where cmake >nul 2>nul
if not errorlevel 1 exit /b 0

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto cmake_missing
set "VS_DIR="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VS_DIR=%%i"
if not defined VS_DIR goto cmake_missing
set "VS_CMAKE=%VS_DIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
if not exist "%VS_CMAKE%\cmake.exe" goto cmake_missing
set "PATH=%VS_CMAKE%;%PATH%"
if not defined VCPKG_ROOT if exist "%VS_DIR%\VC\vcpkg\scripts\buildsystems\vcpkg.cmake" set "VCPKG_ROOT=%VS_DIR%\VC\vcpkg"
exit /b 0

:cmake_missing
echo cmake was not found on PATH or in a Visual Studio installation.
exit /b 1

:usage
echo Usage: mcdebug ^<build^|run^|debug^|offline^|help^>
echo   build  Configure and build the Debug executable.
echo   run    Build the Debug executable, then launch it.
echo   debug  Build, then run it in this console with the debug harness on.
echo          Optional channels: http, body, file, process, log, all. Example: mcdebug debug http,file
echo   offline  Like debug, but every network request fails at once. Use it unless the work needs the APIs.
exit /b 0

:usage_error
echo Usage: mcdebug ^<build^|run^|debug^|offline^|help^>
exit /b 2
