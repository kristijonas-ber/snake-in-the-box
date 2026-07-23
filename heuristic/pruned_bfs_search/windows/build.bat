@echo off
REM build.bat - Build the snake-in-the-box C tools with MSVC (Visual Studio).
REM
REM Run this from an "x64 Native Tools Command Prompt for VS" so that cl.exe is
REM on PATH and targets x64 (required by the _BitScanForward64 intrinsic in
REM bitops.h). It compiles the four executables next to the .c sources.
REM
REM   snake_in_box.exe     - direct pruned BFS demo
REM   extend_snake.exe     - seed the beam and jump to a target dimension
REM   check_snake.exe      - validate one transition sequence + Hamming grid
REM   priming.exe          - extend a seed one dimension at a time
REM   parallel_search.exe  - per-level parallel search (OpenMP)
REM   parallel_extend.exe  - seeded extension with per-level parallelism (OpenMP)
REM
REM MinGW-w64 / Clang-on-Windows users do not need this script: the GNU Makefile
REM in the parent folder works for them unchanged.

setlocal
REM Build the MSVC-portable copies in THIS folder (they include bitops.h and use
REM sib_popcount64/sib_ctz64). Do NOT build the parent's GCC sources: those call
REM __builtin_popcountll/__builtin_ctzl, which MSVC links as unresolved externals.
pushd "%~dp0"

REM /std:c11 requires VS2019 16.8+. On older toolsets, remove it (MSVC's default
REM C mode accepts this code). /D_CRT_SECURE_NO_WARNINGS silences C4996 for
REM fopen/fscanf/atoi/strncpy (warnings only).
set CFLAGS=/nologo /O2 /std:c11 /D_CRT_SECURE_NO_WARNINGS
set SHARED=hypercube.c transitions.c validation.c canonical.c snake_node.c fitness.c snake_io.c

REM Clear leftovers up front too (an interrupted build can strand objects that
REM would otherwise be picked up by the next link).
del /q *.obj *.pdb *.ilk >nul 2>&1

echo Building snake_in_box.exe ...
cl %CFLAGS% %SHARED% bfs_pruned.c main.c /Fe:snake_in_box.exe || goto :error

echo Building extend_snake.exe ...
cl %CFLAGS% %SHARED% extend_snake.c /Fe:extend_snake.exe || goto :error

echo Building check_snake.exe ...
REM Standalone checker: only the validator and the transition->vertex helper.
cl %CFLAGS% validation.c transitions.c check_snake.c /Fe:check_snake.exe || goto :error

echo Building priming.exe ...
cl %CFLAGS% %SHARED% priming.c /Fe:priming.exe || goto :error

echo Building parallel_search.exe (OpenMP) ...
cl %CFLAGS% /openmp %SHARED% parallel_search.c /Fe:parallel_search.exe || goto :error

echo Building parallel_extend.exe (OpenMP) ...
cl %CFLAGS% /openmp %SHARED% parallel_extend.c /Fe:parallel_extend.exe || goto :error

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
