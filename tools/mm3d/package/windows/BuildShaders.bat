@echo off
rem Shader tour: Link visits every area so the first visits never stutter.
rem Game windows open and close by themselves (15-40 minutes). Safe to stop and run again.
cd /d "%~dp0"
if not exist "tools\python\python.exe" (
  echo Run Setup.bat first.
  pause
  exit /b 1
)
tools\python\python.exe setup\TriAevum\tools\mm3d\package\shader_tour.py --install "%~dp0." %*
echo.
pause
