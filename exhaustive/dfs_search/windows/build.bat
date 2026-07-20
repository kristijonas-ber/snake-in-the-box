@echo off
REM ============================================================================
REM  Build the Windows binaries for the dfs_search exhaustive track.
REM
REM    prefixgen_tool.exe    standalone prefix generator - needs ONLY cl.exe,
REM                          no MS-MPI. Always built.
REM    dfs_one.exe           serial DFS over .pfx file(s) - needs ONLY cl.exe,
REM                          no MS-MPI. Always built.
REM    dfs_search.exe        dispatcher build (needs MS-MPI)
REM    dfs_search_replay.exe worker-side replay (needs MS-MPI)
REM
REM  Prerequisites:
REM    * Visual Studio with the C++ toolset (provides cl.exe).
REM    * For the MPI binaries only: Microsoft MPI + MS-MPI SDK
REM      (https://learn.microsoft.com/message-passing-interface/microsoft-mpi).
REM      The SDK installer sets MSMPI_INC and MSMPI_LIB64. If those are not set,
REM      only prefixgen_tool.exe is built.
REM
REM  Run this from an "x64 Native Tools Command Prompt for VS" so cl.exe is on PATH.
REM
REM  Override compile-time knobs by passing them as /D flags, e.g.:
REM    build.bat /DN=8 /DPREFIX_LENGTH=40
REM  (defaults come from config.hpp: N=6, PREFIX_LENGTH=11, one slice = full search)
REM
REM  Then run, e.g.:   prefixgen_tool.exe prefixes 1000000
REM                    mpiexec -n 5 dfs_search.exe
REM ============================================================================
setlocal
REM Build the sources in THIS folder regardless of the caller's current
REM directory (otherwise cl would compile whatever .cpp happens to sit in the
REM CWD, or nothing at all).
pushd "%~dp0"

set CXXFLAGS=/O2 /EHsc /std:c++17 /nologo /W3 /D_CRT_SECURE_NO_WARNINGS

REM Pass all args through verbatim as -D overrides (e.g. /DN=8 /DPREFIX_LENGTH=19).
set DEFS=%*

REM Objects carry the -D values they were compiled with, so ANY leftover .obj can
REM silently relink a stale N / PREFIX_LENGTH into the new binaries. Wipe every
REM object and executable in this folder, not a hardcoded list.
echo Removing stale objects and binaries ...
del /q *.obj *.exe *.pdb *.ilk 2>nul

REM ---- no-MPI binaries: need ONLY cl.exe --------------------------------------
echo Compiling prefixgen_tool + dfs_one (DEFS=%DEFS%) ...
cl %CXXFLAGS% %DEFS% /c prefixgen.cpp search.cpp driver_prefixgen.cpp driver_dfsone.cpp
if errorlevel 1 exit /b 1
cl /nologo prefixgen.obj driver_prefixgen.obj /Fe:prefixgen_tool.exe
if errorlevel 1 exit /b 1
cl /nologo search.obj driver_dfsone.obj      /Fe:dfs_one.exe
if errorlevel 1 exit /b 1
echo Built prefixgen_tool.exe and dfs_one.exe

REM ---- MPI binaries: need MS-MPI ----------------------------------------------
if "%MSMPI_INC%"=="" (
  echo.
  echo NOTE: MSMPI_INC not set - skipping dfs_search.exe / dfs_search_replay.exe.
  echo       prefixgen_tool.exe and dfs_one.exe were built and need no MPI. Install
  echo       the MS-MPI SDK and reopen the VS prompt to build the search binaries.
  endlocal
  exit /b 0
)

REM The MS-MPI installer sets these WITH a trailing backslash. Left in place,
REM /I"%MSMPI_INC%" expands to /I"...\Include\" - the \" escapes the closing
REM quote, the source filename gets swallowed into the string, and cl fails with
REM "command line error D8003: missing source filename". Strip it.
set "MPIINC=%MSMPI_INC%"
if "%MPIINC:~-1%"=="\" set "MPIINC=%MPIINC:~0,-1%"
set "MPILIB=%MSMPI_LIB64%"
if "%MPILIB:~-1%"=="\" set "MPILIB=%MPILIB:~0,-1%"

set LIBS="%MPILIB%\msmpi.lib"

echo Compiling MPI translation units ...
cl %CXXFLAGS% /I"%MPIINC%" %DEFS% /c driver_main.cpp
if errorlevel 1 exit /b 1
cl %CXXFLAGS% /I"%MPIINC%" %DEFS% /c driver_replay.cpp
if errorlevel 1 exit /b 1

echo Linking dfs_search.exe ...
cl /nologo prefixgen.obj search.obj driver_main.obj   /Fe:dfs_search.exe        /link %LIBS%
if errorlevel 1 exit /b 1

echo Linking dfs_search_replay.exe ...
cl /nologo prefixgen.obj search.obj driver_replay.obj /Fe:dfs_search_replay.exe /link %LIBS%
if errorlevel 1 exit /b 1

echo.
echo Built prefixgen_tool.exe, dfs_one.exe, dfs_search.exe and dfs_search_replay.exe
echo Run:  prefixgen_tool.exe prefixes 1000000
echo       dfs_one.exe prefixes\batch_00000.pfx
echo       mpiexec -n 5 dfs_search.exe
endlocal
