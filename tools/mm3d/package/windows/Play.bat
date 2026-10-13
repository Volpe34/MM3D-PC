@echo off
rem Majora's Mask 3D (TriAevum PC port). F1 = settings, F11 = fullscreen.
cd /d "%~dp0"
if not exist "private\mm3d_title_aot.dll" (
  echo The game is not prepared yet: run Setup.bat first.
  pause
  exit /b 1
)
if not exist logs mkdir logs
if exist logs\last_run.log move /y logs\last_run.log logs\previous_run.log >nul
set TRIAEVUM_LOG_FILE=%~dp0logs\last_run.log
set TRIAEVUM_CRASH_DIR=%~dp0logs
set TRIAEVUM_REPORT_TITLE=MM3D
if exist tools\python\python.exe tools\python\python.exe setup\TriAevum\tools\mm3d\package\prepare_shaders.py "%~dp0."
set SHADERS=
if exist data\shader_corpus\mm3d.o3ps if exist data\shader_corpus\all_pipelines.json set SHADERS=--pica-aot-shader-pack data\shader_corpus\mm3d.o3ps --pica-pipeline-manifest data\shader_corpus\all_pipelines.json --pica-pipeline-prewarm
if not exist data\shader_corpus\runs mkdir data\shader_corpus\runs
set STAMP=%RANDOM%%RANDOM%
TriAevum.exe --title-plugin private\mm3d_title_aot.dll --a32-process-manifest process-manifest.json ^
  --resource-root resources --renderer nri --ui-profile oot3d --config data\config\TriAevum.json ^
  --gameplay-timing native30_interpolated --presentation-rate 60 --save-data data\savedata ^
  --width 1280 --height 720 ^
  --pica-effective-shader-inventory data\shader_corpus\runs\%STAMP%_shaders.json ^
  --pica-pipeline-inventory data\shader_corpus\runs\%STAMP%_pipelines.json %SHADERS% %* > logs\last_run.log 2>&1
if errorlevel 1 (
  echo The game stopped with an error. Send logs\last_run.log ^(and any report in logs^) to get help.
  pause
)
