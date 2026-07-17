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
REM    <ram_gb>      base per-step memory limit in GB; the beam prunes to fit it.
REM                  Used for any dimension the schedule below does not cover.
REM    [extra flags] --both-ends and any other extend_snake flags, plus:
REM      --workers N   run each step with parallel_extend.exe on N OpenMP threads
REM        instead of the serial extend_snake.exe. Speeds up per-level expansion
REM        (one node, shared memory); sub-linear past ~8-16 threads.
REM      --ram-schedule D:GB,D:GB,...  per-dimension RAM overrides. A target
REM        dimension uses the nearest listed value at or below it; anything below
REM        the lowest entry uses <ram_gb>. Node size grows as 2^D, so spending
REM        more RAM only at the top dimensions is usually the right shape.
REM
REM  Examples (a Q_13 snake -> ... -> Q_20):
REM    chain_extend.bat dim13_seed.txt 13 20 64 --both-ends
REM    chain_extend.bat dim13_seed.txt 13 20 8 --ram-schedule 18:64,20:128 --both-ends
REM        (dims 14-17 use 8 GB, 18-19 use 64 GB, 20 uses 128 GB)
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
set "RAMSCHED="
set "WORKERS="
:collect
if "%~1"=="" goto :collected
if /I "%~1"=="--ram-schedule" (
  set "RAMSCHED=%~2"
  shift
  shift
  goto :collect
)
if /I "%~1"=="--workers" (
  set "WORKERS=%~2"
  shift
  shift
  goto :collect
)
if /I "%~1"=="--both-ends" set "BOTHTAG=_both"
set "EXTRA=!EXTRA! %~1"
shift
goto :collect
:collected

REM ---- Parse an optional per-dimension RAM schedule "D:GB,D:GB,..." ----------
REM Each entry sets RSCHED_<D>. A target dimension uses the value of the nearest
REM listed dimension at or below it (threshold semantics); dimensions below the
REM lowest entry fall back to the base <ram_gb>. So "13:8,18:64,20:128" with a
REM 13->20 chain means dims 14-17 use 8, 18-19 use 64, and 20 uses 128.
if defined RAMSCHED (
  for %%e in (%RAMSCHED:,= %) do (
    for /f "tokens=1,2 delims=:" %%a in ("%%e") do set "RSCHED_%%a=%%b"
  )
)

REM ---- Locate the binary (build.bat builds it here, in the windows folder). --
REM --workers N -> parallel_extend.exe (OpenMP); otherwise serial extend_snake.exe.
set "SCRIPTDIR=%~dp0"
if defined WORKERS (
  set "EXE=%SCRIPTDIR%parallel_extend.exe"
  set "EXENAME=parallel_extend.exe"
) else (
  set "EXE=%SCRIPTDIR%extend_snake.exe"
  set "EXENAME=extend_snake.exe"
)
if not exist "%EXE%" (
  echo ERROR: %EXENAME% not found at "%EXE%".
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

set "WTAG="
if defined WORKERS set "WTAG=_w%WORKERS%"
if defined CHAIN_NAME (
  set "TAG=%CHAIN_NAME%"
) else if defined RAMSCHED (
  set "SCHEDTAG=!RAMSCHED::=-!"
  set "SCHEDTAG=!SCHEDTAG:,=_!"
  set "TAG=dim%STARTDIM%-%ENDDIM%_ram!SCHEDTAG!%WTAG%%BOTHTAG%"
) else (
  set "TAG=dim%STARTDIM%-%ENDDIM%_ram%RAM%%WTAG%%BOTHTAG%"
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
if defined RAMSCHED (
  echo    RAM budget: %RAM% GB base, schedule %RAMSCHED%
) else (
  echo    RAM budget: %RAM% GB per step
)
if defined WORKERS (
  echo    engine    : parallel_extend.exe, %WORKERS% workers
) else (
  echo    engine    : extend_snake.exe ^(serial^)
)
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

  REM Resolve this dimension's RAM budget (schedule override, else base).
  call :resolve_ram %%D

  echo(
  echo ------------------------------------------------------------
  echo  Extending into dimension %%D  ^(RAM !STEPRAM! GB^)
  echo  Seed: !CURSEED!
  echo ------------------------------------------------------------

  pushd "!OUTDIR!"
  if defined WORKERS (
    "%EXE%" %%D !STEPRAM! %WORKERS%!EXTRA! "!CURSEED!"
  ) else (
    "%EXE%" %%D !STEPRAM!!EXTRA! "!CURSEED!"
  )
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

REM ---- Subroutine: set STEPRAM for target dimension %1. ----------------------
REM Scans downward from the target to start_dim for the nearest RSCHED_<k>;
REM falls back to the base RAM budget when none is set at or below it.
:resolve_ram
set "STEPRAM=%RAM%"
set "_d=%~1"
:rr_loop
if %_d% LSS %STARTDIM% goto :eof
if defined RSCHED_%_d% (
  set "STEPRAM=!RSCHED_%_d%!"
  goto :eof
)
set /a _d-=1
goto :rr_loop

:usage
echo Usage: %~nx0 ^<seed_file^> ^<start_dim^> ^<end_dim^> ^<ram_gb^> [extra flags]
echo   ^<ram_gb^>  base per-step memory limit; used where the schedule is silent.
echo   Extra flags (any order, all optional):
echo     --workers N            parallel per-step search on N OpenMP threads
echo     --both-ends            grow both endpoints (passed to the extender)
echo     --ram-schedule LIST    per-dimension RAM, e.g. --ram-schedule 13:8,18:64,20:128
echo   e.g. %~nx0 dim13_seed.txt 13 20 8 --ram-schedule 18:64,20:128 --workers 16 --both-ends
exit /b 1
