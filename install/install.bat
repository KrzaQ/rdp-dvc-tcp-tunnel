@echo off
setlocal

set "CLSID={8B6D78AA-856B-4D4F-A2A2-0C0CCC4B4E18}"
set "DLL_PATH=%~dp0kq-tunnel-plugin.dll"

if not exist "%DLL_PATH%" (
    echo Error: kq-tunnel-plugin.dll not found in %~dp0
    echo Copy it next to this script before running.
    exit /b 1
)

echo Registering kq-tunnel DVC plugin...
echo   DLL: %DLL_PATH%
echo   CLSID: %CLSID%

reg add "HKCU\Software\Classes\CLSID\%CLSID%" /ve /d "KQ Tunnel DVC Plugin" /f
reg add "HKCU\Software\Classes\CLSID\%CLSID%\InprocServer32" /ve /d "%DLL_PATH%" /f
reg add "HKCU\Software\Classes\CLSID\%CLSID%\InprocServer32" /v "ThreadingModel" /d "Free" /f
reg add "HKCU\Software\Microsoft\Terminal Server Client\Default\AddIns\KqTunnel" /v "Name" /d "%CLSID%" /f

echo Done. Restart your RDP client for changes to take effect.
