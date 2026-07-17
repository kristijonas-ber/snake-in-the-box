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
REM  Output layout (created under the current directory, override with CHAIN_OUT):
REM    chain_out\dim14\seeds\dim14_len<L>.txt   <- reusable seed (fed to dim 15)
REM    chain_out\dim14\snakes\dim14_len<L>.txt  <- readable record + vertices
REM    chain_out\dim15\...                       and so on up to end_dim.
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
:collect
if not "%~1"=="" (
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

if not defined CHAIN_OUT set "CHAIN_OUT=%CD%\chain_out"
set "OUTROOT=%CHAIN_OUT%"

echo ============================================================
echo  Chained seed extension
echo    seed      : %SEED%
echo    dimensions: %STARTDIM% -^> %ENDDIM%
echo    RAM budget: %RAM% GB per step
echo    extra args:%EXTRA%
echo    output    : %OUTROOT%
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
