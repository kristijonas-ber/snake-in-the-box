@echo off
REM ============================================================================
REM  Build dfs_search (dispatcher) and dfs_search_replay for Windows + MS-MPI.
REM
REM  Prerequisites:
REM    * Visual Studio with the C++ toolset (provides cl.exe).
REM    * Microsoft MPI + MS-MPI SDK  (https://learn.microsoft.com/message-passing-interface/microsoft-mpi)
REM      The SDK installer sets the env vars MSMPI_INC and MSMPI_LIB64.
REM
REM  Run this from an "x64 Native Tools Command Prompt for VS" so cl.exe is on PATH.
REM
REM  Override compile-time knobs by passing them as /D flags, e.g.:
REM    build.bat /DN=6 /DPREFIX_LENGTH=11 /DSLICE_COUNT=4 /DSLICE_ID=0
REM  (defaults come from config.hpp: N=6, PREFIX_LENGTH=11, one slice = full search)
REM
REM  Then run, e.g.:   mpiexec -n 5 dfs_search.exe
REM                    mpiexec -n 5 dfs_search_replay.exe
REM ============================================================================
setlocal

if "%MSMPI_INC%"=="" (
  echo ERROR: MSMPI_INC not set. Install the MS-MPI SDK and reopen the VS prompt.
  exit /b 1
)

set DEFS=%*
set CXXFLAGS=/O2 /EHsc /std:c++17 /nologo /W3 /I"%MSMPI_INC%"
set LIBS="%MSMPI_LIB64%\msmpi.lib"

echo Compiling (DEFS=%DEFS%) ...
cl %CXXFLAGS% %DEFS% /c prefixgen.cpp search.cpp driver_main.cpp driver_replay.cpp
if errorlevel 1 exit /b 1

echo Linking dfs_search.exe ...
cl /nologo prefixgen.obj search.obj driver_main.obj   /Fe:dfs_search.exe        /link %LIBS%
if errorlevel 1 exit /b 1

echo Linking dfs_search_replay.exe ...
cl /nologo prefixgen.obj search.obj driver_replay.obj /Fe:dfs_search_replay.exe /link %LIBS%
if errorlevel 1 exit /b 1

echo.
echo Built dfs_search.exe and dfs_search_replay.exe
echo Run:  mpiexec -n 5 dfs_search.exe
endlocal
