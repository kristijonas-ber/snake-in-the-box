@echo off
REM ============================================================================
REM  chain_extend.bat - Chained heuristic seed extension across dimensions.
REM
REM  Takes a snake living in Q_<start_dim> and grows it one dimension at a time
REM  up to Q_<end_dim>: extend into <start_dim>+1, feed that result in as the seed
REM  for <start_dim>+2, and so on. Each step is one run of extend_snake.exe under
REM  a fixed RAM budget (that budget is what the beam prunes against). Every
REM  dimension's result is saved, so the chain leaves a full audit trail.
REM
REM  Usage:
REM    chain_extend.bat <seed_file> <start_dim> <end_dim> <ram_gb> [extra flags]
REM
REM    <seed_file>   text file of space/newline-separated transition integers
REM                  (exactly what the beam / a prior step writes to seeds\).
REM    <start_dim>   dimension the seed already lives in (e.g. 13).
REM    <end_dim>     dimension to reach (e.g. 20).
REM    <ram_gb>      per-step memory limit in GB; the beam prunes to fit it.
REM    [extra flags] passed straight to extend_snake.exe, e.g. --both-ends.
REM
REM  Example (a Q_13 snake -> ... -> Q_20, 64 GB budget, grow both endpoints):
REM    chain_extend.bat dim13_seed.txt 13 20 64 --both-ends
REM
REM  Each run gets its OWN subfolder under the results root, tagged by its
REM  parameters, so separate chains (e.g. different RAM budgets) never collide:
REM    results\dim13-20_ram64_both\dim14\seeds\dim14_len<L>.txt  <- fed to dim 15
REM    results\dim13-20_ram64_both\dim14\snakes\dim14_len<L>.txt <- readable record
REM    results\dim13-20_ram64_both\dim15\...                      up to end_dim
REM    results\dim13-20_ram128\...                               a different chain
REM  If a tag already exists, a _2, _3, ... suffix is added so nothing is
REM  overwritten. Overrides:
REM    CHAIN_ROOT  results root (default: results\ in the current directory)
REM    CHAIN_NAME  exact name for this run's subfolder (skips the auto tag)
REM
REM  extend_snake.exe must already be built (run build.bat from an x64 Native
REM  Tools Command Prompt for VS first).
REM ============================================================================
setlocal enabledelayedexpansion

if "%~4"=="" goto :usage

set "SEED=%~f1"
set "STARTDIM=%~2"
set "ENDDIM=%~3"
set "RAM=%~4"

REM ---- Everything after the 4th argument is passed through to extend_snake. ----
shift
shift
shift
shift
set "EXTRA="
set "BOTHTAG="
:collect
if not "%~1"=="" (
  if /I "%~1"=="--both-ends" set "BOTHTAG=_both"
  set "EXTRA=!EXTRA! %~1"
  shift
  goto :collect
)

REM ---- Locate the binary (built next to the .c sources, one level up). --------
set "SCRIPTDIR=%~dp0"
set "EXE=%SCRIPTDIR%..\extend_snake.exe"
if not exist "%EXE%" (
  echo ERROR: extend_snake.exe not found at "%EXE%".
  echo Build it first from an x64 Native Tools Command Prompt for VS:
  echo     cd /d "%SCRIPTDIR%"  ^&^&  build.bat
  exit /b 1
)

REM ---- Validate inputs. -------------------------------------------------------
if not exist "%SEED%" (
  echo ERROR: seed file "%SEED%" does not exist.
  exit /b 1
)
if %ENDDIM% LEQ %STARTDIM% (
  echo ERROR: end_dim ^(%ENDDIM%^) must be greater than start_dim ^(%STARTDIM%^).
  exit /b 1
)

REM ---- Results root + a separate subfolder per chain run. --------------------
if not defined CHAIN_ROOT set "CHAIN_ROOT=%CD%\results"

if defined CHAIN_NAME (
  set "TAG=%CHAIN_NAME%"
) else (
  set "TAG=dim%STARTDIM%-%ENDDIM%_ram%RAM%%BOTHTAG%"
)

set "RUNDIR=%CHAIN_ROOT%\%TAG%"
set /a _n=1
:findfree
if exist "%RUNDIR%" (
  set /a _n+=1
  set "RUNDIR=%CHAIN_ROOT%\%TAG%_!_n!"
  goto :findfree
)
mkdir "%RUNDIR%"
set "OUTROOT=%RUNDIR%"

echo ============================================================
echo  Chained seed extension
echo    seed      : %SEED%
echo    dimensions: %STARTDIM% -^> %ENDDIM%
echo    RAM budget: %RAM% GB per step
echo    extra args:%EXTRA%
echo    run folder: %OUTROOT%
echo ============================================================

set "CURSEED=%SEED%"
set /a FIRST=%STARTDIM%+1

for /L %%D in (%FIRST%,1,%ENDDIM%) do (
  set "OUTDIR=%OUTROOT%\dim%%D"

  REM Fresh per-step dir so its seeds\ holds exactly one result file.
  if exist "!OUTDIR!\seeds"  del /q "!OUTDIR!\seeds\*.txt"  >nul 2>&1
  if not exist "!OUTDIR!" mkdir "!OUTDIR!"

  echo(
  echo ------------------------------------------------------------
  echo  Extending into dimension %%D  ^(RAM %RAM% GB^)
  echo  Seed: !CURSEED!
  echo ------------------------------------------------------------

  pushd "!OUTDIR!"
  "%EXE%" %%D %RAM%!EXTRA! "!CURSEED!"
  set "RC=!ERRORLEVEL!"
  popd

  if not "!RC!"=="0" (
    echo(
    echo ERROR: extend_snake failed at dimension %%D ^(exit code !RC!^). Stopping.
    exit /b 1
  )

  REM The step's seeds\ dir now holds exactly one file: that is the next seed.
  set "NEXTSEED="
  set "NEXTLEN="
  for %%F in ("!OUTDIR!\seeds\*.txt") do (
    set "NEXTSEED=%%~fF"
    for /f "tokens=2 delims=_" %%p in ("%%~nF") do set "NEXTLEN=%%p"
  )
  if not defined NEXTSEED (
    echo(
    echo ERROR: no result seed was written for dimension %%D. Stopping.
    exit /b 1
  )

  set "NEXTLEN=!NEXTLEN:len=!"
  set "CURSEED=!NEXTSEED!"
  echo Saved dim %%D result ^(!NEXTLEN! edges^) -^> !CURSEED!
)

echo(
echo ============================================================
echo  Chain complete. Final snake: %CURSEED%
echo ============================================================
endlocal
exit /b 0

:usage
echo Usage: %~nx0 ^<seed_file^> ^<start_dim^> ^<end_dim^> ^<ram_gb^> [extra extend_snake flags]
echo   e.g. %~nx0 dim13_seed.txt 13 20 64 --both-ends
exit /b 1
