@echo off
setlocal

rem Aporia API - build any engine branch in an isolated sandbox.
rem Usage: tools\build.bat [branch]     (main / experimental/dev / directx)

set "BRANCH=%~1"
if "%BRANCH%"=="" set "BRANCH=main"

set "REPO=https://gitlab.com/aporia-xyz/aporia-engine.git"
set "ROOT=%~dp0..\.."
set "SLUG=%BRANCH:/=_%"
set "SANDBOX=%ROOT%\Aporia-sandbox\%SLUG%"

where git >nul 2>&1
if errorlevel 1 (
    echo [aporia] git is not in PATH. Install Git for Windows and run again.
    exit /b 1
)

if not exist "%SANDBOX%\.git" (
    echo [aporia] cloning %BRANCH% into "%SANDBOX%" ...
    git clone --branch "%BRANCH%" "%REPO%" "%SANDBOX%"
    if errorlevel 1 exit /b 1
) else (
    echo [aporia] updating %BRANCH% in "%SANDBOX%" ...
    git -C "%SANDBOX%" fetch origin
    git -C "%SANDBOX%" checkout "%BRANCH%"
    git -C "%SANDBOX%" pull --ff-only
    if errorlevel 1 exit /b 1
)

echo [aporia] syncing API headers ...
robocopy "%~dp0..\include\Aporia" "%SANDBOX%\extern\Aporia-API\include\Aporia" *.hpp /MIR /NFL /NDL /NJH /NJS >nul
if errorlevel 8 (
    echo [aporia] API sync failed.
    exit /b 1
)
copy /y "%~dp0..\README.md" "%SANDBOX%\extern\Aporia-API\README.md" >nul

echo [aporia] configuring ...
cmake -S "%SANDBOX%" -B "%SANDBOX%\build" -DAPORIA_ENABLE_GRAPHICS=ON -DAPORIA_ENABLE_VULKAN=ON
if errorlevel 1 exit /b 1

echo [aporia] building ...
cmake --build "%SANDBOX%\build" --config Release
if errorlevel 1 exit /b 1

echo [aporia] testing ...
ctest --test-dir "%SANDBOX%\build" -C Release --output-on-failure
if errorlevel 1 exit /b 1

echo.
echo [aporia] %BRANCH% is green: "%SANDBOX%"
