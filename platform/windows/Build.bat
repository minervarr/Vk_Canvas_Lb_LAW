@echo off
setlocal

:: 1. Check if pwsh (PowerShell Core) is installed, otherwise fallback to standard powershell
where pwsh >nul 2>nul
if %errorlevel% neq 0 (
    set "PS_CMD=powershell"
) else (
    set "PS_CMD=pwsh"
)

cls
echo Running build script using %PS_CMD%...
echo.

:: 2. Run the script (Bypass execution policy just in case it's restricted on your machine)
%PS_CMD% -ExecutionPolicy Bypass -File "build_msvc.ps1"

:: 3. If the script failed, pause so you can read the error before the window closes
if %errorlevel% neq 0 (
    echo.
    echo [!] Build failed or was interrupted. Press any key to close...
    pause >nul
)

endlocal
