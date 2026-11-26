@echo off
setlocal

rem Aporia API - create a new project from the engine.
rem Usage: tools\create_project.bat <name> [tag or branch]

set "NAME=%~1"
if "%NAME%"=="" (
    echo usage: create_project.bat ^<name^> [tag^|branch]
    exit /b 1
)
set "REF=%~2"
if "%REF%"=="" set "REF=main"

set "REPO=https://gitlab.com/aporia-xyz/aporia-engine.git"

where git >nul 2>&1
if errorlevel 1 (
    echo [aporia] git is not in PATH. Install Git for Windows and run again.
    exit /b 1
)

if exist "%NAME%" (
    echo [aporia] "%NAME%" already exists, pick another name.
    exit /b 1
)

echo [aporia] cloning engine %REF% ...
git clone --depth 1 --branch "%REF%" "%REPO%" "%NAME%\engine"
if errorlevel 1 exit /b 1

echo [aporia] syncing API headers ...
robocopy "%~dp0..\include\Aporia" "%NAME%\engine\extern\Aporia-API\include\Aporia" *.hpp /MIR /NFL /NDL /NJH /NJS >nul
if errorlevel 8 (
    echo [aporia] API sync failed.
    exit /b 1
)
copy /y "%~dp0..\README.md" "%NAME%\engine\extern\Aporia-API\README.md" >nul

echo [aporia] writing project files ...
mkdir "%NAME%\src" >nul 2>&1
xcopy /E /I /Y "%~dp0template" "%NAME%" >nul

echo [aporia] configuring ...
cmake -S "%NAME%" -B "%NAME%\build" -DAPORIA_ENABLE_GRAPHICS=ON -DAPORIA_ENABLE_VULKAN=ON
if errorlevel 1 exit /b 1

echo [aporia] building ...
cmake --build "%NAME%\build" --config Release
if errorlevel 1 exit /b 1

echo.
echo [aporia] project "%NAME%" is ready.
echo   cd "%NAME%"
echo   build\Release\my_game.exe
