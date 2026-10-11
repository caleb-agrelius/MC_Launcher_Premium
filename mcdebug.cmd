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

:usage
echo Usage: mcdebug ^<build^|run^|help^>
echo   build  Configure and build the Debug executable.
echo   run    Build the Debug executable, then launch it.
exit /b 0

:usage_error
echo Usage: mcdebug ^<build^|run^|help^>
exit /b 2
