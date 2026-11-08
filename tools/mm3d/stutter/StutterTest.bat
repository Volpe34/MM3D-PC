@echo off
rem StutterTest.bat <config> [tag]: Play.bat with the frame-hitch log (MM3D_HITCH_LOG) on.
rem   config: asis (default) | nowait | nohd | preload | asyncall | guestthread, combined with +
rem           (e.g. nowait+guestthread; see tools\stutter\stutter_config.py)
rem   tag:    optional name for this run, e.g. s1 / s2 for the first / second session.
rem The run uses a copy of your settings (data\config\TriAevum.stutter.json): data\config\TriAevum.json
rem is not changed, and settings changed in F1 during the run only go to the copy.
rem F9 during play writes a numbered marker into the log (press it when a pass of the route starts).
rem Result: logs\stutter_<config>[_<tag>]_<date-time>.log (+ .run.log and .config.json next to it).
setlocal
cd /d "%~dp0"
if not exist "private\mm3d_title_aot.dll" (
  echo The game is not prepared yet: run Setup.bat first.
  pause
  exit /b 1
)
set CONFIG=%~1
if "%CONFIG%"=="" set CONFIG=asis
set LABEL=%CONFIG%
if not "%~2"=="" set LABEL=%CONFIG%_%~2
for /f %%t in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd-HHmmss"') do set STAMP=%%t
if not exist logs mkdir logs
if not exist data\textures_off mkdir data\textures_off
set OUT=%~dp0logs\stutter_%LABEL%_%STAMP%
tools\python\python.exe tools\stutter\stutter_config.py %CONFIG% data\config\TriAevum.json data\config\TriAevum.stutter.json
if errorlevel 1 (
  pause
  exit /b 1
)
copy /y data\config\TriAevum.stutter.json "%OUT%.config.json" >nul

set MM3D_HITCH_LOG=1
set MM3D_HITCH_LOG_FILE=%OUT%.log
set TRIAEVUM_ASYNC_ALL_SHADERS=
echo %CONFIG% | findstr /i "asyncall" >nul && set TRIAEVUM_ASYNC_ALL_SHADERS=1
set TRIAEVUM_LOG_FILE=%OUT%.run.log
set TRIAEVUM_CRASH_DIR=%~dp0logs
set TRIAEVUM_REPORT_TITLE=MM3D

rem The same shader preparation and arguments as Play.bat.
if exist tools\python\python.exe tools\python\python.exe setup\TriAevum\tools\mm3d\package\prepare_shaders.py "%~dp0."
if exist data\shader_corpus\last_build.log copy /y data\shader_corpus\last_build.log "%OUT%.shader_build.log" >nul
set SHADERS=
if exist data\shader_corpus\mm3d.o3ps if exist data\shader_corpus\all_pipelines.json set SHADERS=--pica-aot-shader-pack data\shader_corpus\mm3d.o3ps --pica-pipeline-manifest data\shader_corpus\all_pipelines.json --pica-pipeline-prewarm
if not exist data\shader_corpus\runs mkdir data\shader_corpus\runs
set RUN=%RANDOM%%RANDOM%
echo Recording %OUT%.log  (F9 = marker; close the game normally when done)
"%~dp0TriAevum.exe" --title-plugin private\mm3d_title_aot.dll --a32-process-manifest process-manifest.json ^
  --resource-root resources --renderer nri --ui-profile oot3d --config data\config\TriAevum.stutter.json ^
  --gameplay-timing native30_interpolated --presentation-rate 60 --save-data data\savedata ^
  --width 1280 --height 720 ^
  --pica-effective-shader-inventory data\shader_corpus\runs\%RUN%_shaders.json ^
  --pica-pipeline-inventory data\shader_corpus\runs\%RUN%_pipelines.json %SHADERS% %3 %4 %5 %6 > "%OUT%.run.log" 2>&1
if errorlevel 1 echo The game stopped with an error: see "%OUT%.run.log".
echo.
tools\python\python.exe tools\stutter\analyze_hitch_log.py "%OUT%.log"
echo.
echo Log: %OUT%.log
pause
