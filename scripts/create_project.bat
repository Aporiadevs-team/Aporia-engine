@echo off
setlocal

rem Aporia Engine - project initializer.
rem Usage: create_project.bat [project folder] [engine tag]

set "REPO=https://github.com/Aporiadevs-team/Aporia-engine.git"
set "PROJECT=%~1"
if "%PROJECT%"=="" set "PROJECT=aporia-project"
set "TAG=%~2"
if "%TAG%"=="" set "TAG=v1.0.0"

where git >nul 2>&1
if errorlevel 1 (
    echo [aporia] git is not in PATH. Install Git for Windows and run again.
    exit /b 1
)

if exist "%PROJECT%" (
    echo [aporia] "%PROJECT%" already exists, pick another name.
    exit /b 1
)

echo [aporia] cloning engine %TAG% ...
git clone --depth 1 --branch "%TAG%" "%REPO%" "%PROJECT%\engine"
if errorlevel 1 (
    echo [aporia] clone failed.
    exit /b 1
)

mkdir "%PROJECT%\src" >nul 2>&1
copy /y "%PROJECT%\engine\scripts\template\CMakeLists.txt" "%PROJECT%\CMakeLists.txt" >nul
copy /y "%PROJECT%\engine\scripts\template\main.cpp" "%PROJECT%\src\main.cpp" >nul

echo.
echo [aporia] project "%PROJECT%" is ready.
echo   cd "%PROJECT%"
echo   cmake -S . -B build -DAPORIA_ENABLE_GRAPHICS=ON -DAPORIA_ENABLE_VULKAN=ON
echo   cmake --build build --config Release
echo.
