@echo off
title EAC Cleanup
color 0C
echo ============================================
echo   EAC Service / Driver Killer
echo   Run as Administrator!
echo ============================================
echo.

:: Check admin
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo [!] ERROR: Not running as Administrator!
    echo [!] Right-click this file and "Run as administrator"
    pause
    exit /b 1
)

echo [*] Killing EAC processes...
taskkill /F /IM EasyAntiCheat.exe >nul 2>&1
taskkill /F /IM EasyAntiCheat_EOS.exe >nul 2>&1
taskkill /F /IM EasyAntiCheat_Setup.exe >nul 2>&1
taskkill /F /IM start_protected_game.exe >nul 2>&1
echo [+] Processes killed.

echo.
echo [*] Stopping EAC services...
sc stop EasyAntiCheat >nul 2>&1
sc stop EasyAntiCheat_EOS >nul 2>&1
sc stop EasyAntiCheatSys >nul 2>&1
echo [+] Services stopped.

echo.
echo [*] Disabling EAC services (prevents auto-restart)...
sc config EasyAntiCheat start= disabled >nul 2>&1
sc config EasyAntiCheat_EOS start= disabled >nul 2>&1
sc config EasyAntiCheatSys start= disabled >nul 2>&1
echo [+] Services disabled.

echo.
echo [*] Unloading EAC kernel driver (EasyAntiCheat.sys)...
sc stop EasyAntiCheat >nul 2>&1

:: Try to find and unload EAC driver variants
for %%d in (EasyAntiCheat EasyAntiCheatSys EasyAntiCheat_EOS) do (
    sc query %%d >nul 2>&1
    if !errorlevel! equ 0 (
        sc stop %%d >nul 2>&1
        sc delete %%d >nul 2>&1
        echo [+] Removed driver: %%d
    )
)

echo.
echo [*] Checking if EAC is still running...
sc query EasyAntiCheat 2>nul | find "RUNNING" >nul
if %errorlevel% equ 0 (
    echo [!] WARNING: EAC service is still running!
    echo [!] Try rebooting or use a driver unloader.
) else (
    echo [+] EAC is NOT running. You're clean.
)

echo.
echo ============================================
echo   Done! Load your driver now.
echo ============================================
echo.
pause
