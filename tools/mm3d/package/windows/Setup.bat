@echo off
rem Majora's Mask 3D (TriAevum PC port): one-time setup from your own ROM.
rem Put your decrypted ROM (.3ds/.cci) in this folder or drag it onto this file.
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup\setup.ps1" %*
echo.
pause
