@echo off
REM build.bat - Build the snake-in-the-box C tools with MSVC (Visual Studio).
REM
REM Run this from an "x64 Native Tools Command Prompt for VS" so that cl.exe is
REM on PATH and targets x64 (required by the _BitScanForward64 intrinsic in
REM bitops.h). It compiles the four executables next to the .c sources.
REM
REM   snake_in_box.exe    - direct pruned BFS demo
REM   extend_snake.exe    - seed the beam and jump to a target dimension
REM   priming.exe         - extend a seed one dimension at a time
REM   parallel_search.exe - per-level parallel search (OpenMP)
REM
REM MinGW-w64 / Clang-on-Windows users do not need this script: the GNU Makefile
REM in the parent folder works for them unchanged.

setlocal
pushd "%~dp0.."

REM /std:c11 requires VS2019 16.8+. On older toolsets, remove it (MSVC's default
REM C mode accepts this code). /D_CRT_SECURE_NO_WARNINGS silences C4996 for
REM fopen/fscanf/atoi/strncpy (warnings only).
set CFLAGS=/nologo /O2 /std:c11 /D_CRT_SECURE_NO_WARNINGS
set SHARED=hypercube.c transitions.c validation.c canonical.c snake_node.c fitness.c snake_io.c

echo Building snake_in_box.exe ...
cl %CFLAGS% %SHARED% bfs_pruned.c main.c /Fe:snake_in_box.exe || goto :error

echo Building extend_snake.exe ...
cl %CFLAGS% %SHARED% extend_snake.c /Fe:extend_snake.exe || goto :error

echo Building priming.exe ...
cl %CFLAGS% %SHARED% priming.c /Fe:priming.exe || goto :error

echo Building parallel_search.exe (OpenMP) ...
cl %CFLAGS% /openmp %SHARED% parallel_search.c /Fe:parallel_search.exe || goto :error

del *.obj >nul 2>&1
echo.
echo Build complete.
popd
endlocal
exit /b 0

:error
echo.
echo Build FAILED.
del *.obj >nul 2>&1
popd
endlocal
exit /b 1
