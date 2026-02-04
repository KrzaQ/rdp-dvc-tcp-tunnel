@echo off
setlocal

set "CLSID={8B6D78AA-856B-4D4F-A2A2-0C0CCC4B4E18}"

echo Unregistering kq-tunnel DVC plugin...

reg delete "HKCU\Software\Classes\CLSID\%CLSID%" /f 2>nul
reg delete "HKCU\Software\Microsoft\Terminal Server Client\Default\AddIns\KqTunnel" /f 2>nul

echo Done. Restart your RDP client for changes to take effect.
