@echo off
setlocal

set "PROJECT_DIR=%~dp0"
if "%PROJECT_DIR:~-1%"=="\" set "PROJECT_DIR=%PROJECT_DIR:~0,-1%"
set "BUILD_DIR=%PROJECT_DIR%\build"

if /I "%~1"=="build" goto build
if /I "%~1"=="run" goto run
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

if exist "%BUILD_DIR%\Debug\MC_Launcher_Premium.exe" (
    set "APP=%BUILD_DIR%\Debug\MC_Launcher_Premium.exe"
) else if exist "%BUILD_DIR%\MC_Launcher_Premium.exe" (
    set "APP=%BUILD_DIR%\MC_Launcher_Premium.exe"
) else (
    echo Could not find the built MC_Launcher_Premium.exe.
    exit /b 1
)

start "" /D "%PROJECT_DIR%" "%APP%"
exit /b %errorlevel%

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
echo Usage: mcdebug ^<build^|run^|help^>
echo   build  Configure and build the Debug executable.
echo   run    Build the Debug executable, then launch it.
exit /b 0

:usage_error
echo Usage: mcdebug ^<build^|run^|help^>
exit /b 2
