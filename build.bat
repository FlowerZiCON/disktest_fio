@echo off
rem ============================================================================
rem  qtfiogui - Disk Reliability Tester : one-click build script
rem
rem  Double-click  = configure + build Release.
rem  With options  = see  build.bat help
rem
rem  NOTE: This file must stay CRLF. cmd.exe's .bat parser does not accept LF.
rem  NOTE: Messages are deliberately ASCII-only. Non-ASCII text in a .bat is
rem        decoded with whatever code page the console happens to use, and
rem        comes out as garbage when redirected. Chinese docs live in README.md.
rem ============================================================================
for /f "tokens=2 delims=:." %%c in ('chcp') do set "OLDCP=%%c"
setlocal enabledelayedexpansion

set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
set "BUILD_DIR=%ROOT%\build"
set "FIO_DIR=%ROOT%\third_party\fio"
set "DIST_DIR=%BUILD_DIR%\dist"

rem ---------------------------------------------------------------- arguments
rem cmd treats "=" as an argument separator, so "/qt=C:\Qt" arrives as
rem %1=/qt  %2=C:\Qt -- identical to "/qt C:\Qt". Both forms work below.
set "ACTION=build"
set "SELFTEST_TARGET="
set "FILL_LIMIT=1000"
if not defined JOBS set "JOBS=%NUMBER_OF_PROCESSORS%"

:parse_args
if "%~1"=="" goto args_done
set "ARG=%~1"
if /i "!ARG!"=="build"    ( set "ACTION=build"    & shift & goto parse_args )
if /i "!ARG!"=="package"  ( set "ACTION=package"  & shift & goto parse_args )
if /i "!ARG!"=="selftest" ( set "ACTION=selftest" & shift & goto parse_args )
if /i "!ARG!"=="run"      ( set "ACTION=run"      & shift & goto parse_args )
if /i "!ARG!"=="clean"    ( set "ACTION=clean"    & shift & goto parse_args )
if /i "!ARG!"=="headers"  ( set "ACTION=headers"  & shift & goto parse_args )
if /i "!ARG!"=="check"    ( set "ACTION=check"    & shift & goto parse_args )
if /i "!ARG!"=="git"      ( set "ACTION=git"      & shift & goto parse_args )
if /i "!ARG!"=="help"     goto usage_ok
if /i "!ARG!"=="-h"       goto usage_ok
if /i "!ARG!"=="/?"       goto usage_ok
if /i "!ARG!"=="/qt"        ( set "QT_DIR=%~2"          & shift & shift & goto parse_args )
if /i "!ARG!"=="/vs"        ( set "VS_DIR=%~2"          & shift & shift & goto parse_args )
if /i "!ARG!"=="/target"    ( set "SELFTEST_TARGET=%~2" & shift & shift & goto parse_args )
if /i "!ARG!"=="/filllimit" ( set "FILL_LIMIT=%~2"      & shift & shift & goto parse_args )
if /i "!ARG!"=="/jobs"      ( set "JOBS=%~2"            & shift & shift & goto parse_args )
if /i "!ARG!"=="/message"   ( set "GIT_MSG=%~2"         & shift & shift & goto parse_args )
echo [ERROR] Unknown argument: !ARG!
echo.
goto usage_err

:args_done

rem ============================================================================
rem  Actions that do not need a compiler.
rem  check/headers only look at text files, so a machine without MSVC or Qt can
rem  still run them -- that matters for contributors who just want to verify
rem  the license headers before sending a patch.
rem ============================================================================
if /i "!ACTION!"=="clean"   goto do_clean
if /i "!ACTION!"=="headers" goto do_headers
if /i "!ACTION!"=="check"   goto do_check
if /i "!ACTION!"=="git"     goto do_git

rem ---------------------------------------------------------------- toolchain
set "CMAKE="
for %%i in (cmake.exe) do if not "%%~$PATH:i"=="" set "CMAKE=%%~$PATH:i"
if not defined CMAKE if exist "C:\Program Files\CMake\bin\cmake.exe" set "CMAKE=C:\Program Files\CMake\bin\cmake.exe"
if not defined CMAKE (
    echo [ERROR] cmake.exe not found. Install CMake and put it on PATH.
    exit /b 1
)

rem --- ninja: prefer it when present (single-config, much faster than VS) ---
set "NINJA="
for %%i in (ninja.exe) do if not "%%~$PATH:i"=="" set "NINJA=%%~$PATH:i"

rem --- Visual Studio -> vcvars64.bat -----------------------------------------
rem %ProgramFiles(x86)% expands to a string containing ')', which would close
rem an if(...) block early -- keep it in a variable and use !PF86! inside blocks.
set "PF86=%ProgramFiles(x86)%"
if not defined VCVARS (
    if not defined VS_DIR (
        set "VSWHERE=!PF86!\Microsoft Visual Studio\Installer\vswhere.exe"
        if exist "!VSWHERE!" (
            "!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%TEMP%\qtfiogui_vs.txt" 2>nul
            set /p VS_DIR=<"%TEMP%\qtfiogui_vs.txt"
            del "%TEMP%\qtfiogui_vs.txt" >nul 2>&1
        )
    )
    if defined VS_DIR if exist "!VS_DIR!\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=!VS_DIR!\VC\Auxiliary\Build\vcvars64.bat"
)
if not defined VCVARS (
    echo [ERROR] vcvars64.bat not found - no MSVC C++ toolset available.
    echo         Install the "Desktop development with C++" workload,
    echo         or point at it with  /vs ^<VS install dir^>.
    exit /b 1
)

rem NOTE (cosmetic): a full rebuild on a Chinese-locale MSVC floods the log
rem with thousands of /showIncludes lines ("Note: including file:" is printed
rem in Chinese). CMake detects that prefix but re-encodes it as UTF-8 while
rem cl.exe emits GBK, so Ninja can never match it and never strips those
rem lines. VSLANG=1033 does not change cl.exe's language here either, so
rem nothing in this script can fix it. Incremental builds stay quiet.
rem Workaround if it bothers you: configure once with the VS generator --
rem this script reuses whatever generator build\ already has:
rem   cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DCMAKE_PREFIX_PATH=<Qt dir>
echo [1/4] Loading MSVC environment ...
call "!VCVARS!" >nul
if errorlevel 1 (
    echo [ERROR] vcvars64.bat failed: !VCVARS!
    exit /b 1
)

rem --- Qt --------------------------------------------------------------------
rem /qt  >  QT_DIR env  >  scan C:\Qt  D:\Qt  %USERPROFILE%\Qt
if not defined QT_DIR (
    for %%r in ("C:\Qt" "D:\Qt" "%USERPROFILE%\Qt") do (
        if not defined QT_DIR if exist "%%~r" (
            for /f "delims=" %%v in ('dir /b /ad /o-n "%%~r" 2^>nul') do (
                if not defined QT_DIR for /f "delims=" %%q in ('dir /b /ad "%%~r\%%v\msvc*_64" 2^>nul') do set "QT_DIR=%%~r\%%v\%%q"
            )
        )
    )
)
if not defined QT_DIR (
    echo [ERROR] Qt not found. Use  /qt ^<Qt dir^>  or set the QT_DIR env var,
    echo         e.g.  build.bat /qt C:\Qt\6.8.3\msvc2022_64
    exit /b 1
)
if not exist "!QT_DIR!\lib\cmake\Qt6\Qt6Config.cmake" (
    echo [WARN] Qt6Config.cmake not found under !QT_DIR! - is that really a Qt dir?
)

rem ---------------------------------------------------------------- generator
rem Reuse whatever generator an existing build\ was configured with.
rem Switching generator mid-flight requires a reconfigure, so we never do it.
set "CACHED_GEN="
if exist "!BUILD_DIR!\CMakeCache.txt" for /f "usebackq tokens=2 delims==" %%g in (`findstr /b /c:"CMAKE_GENERATOR:INTERNAL=" "!BUILD_DIR!\CMakeCache.txt"`) do set "CACHED_GEN=%%g"

set "GEN_ARGS="
if defined CACHED_GEN (
    set "GEN_ARGS=-G "!CACHED_GEN!""
) else if defined NINJA (
    set "GEN_ARGS=-G Ninja -DCMAKE_BUILD_TYPE=Release"
)

rem ---------------------------------------------------------------- clean
rem Every configuring action wipes build\ and reconfigures first.
rem Incremental builds proved unreliable in this project: after editing a
rem header we once got an exe whose behaviour did not match the sources,
rem and a clean rebuild fixed it. A full rebuild costs a few dozen seconds
rem and guarantees the exe matches the code on disk.
if exist "!BUILD_DIR!" (
    echo [2/4] Removing !BUILD_DIR! for a full rebuild ...
    rmdir /s /q "!BUILD_DIR!"
    set "CACHED_GEN="
    set "GEN_ARGS="
)
if not defined CACHED_GEN if defined NINJA set "GEN_ARGS=-G Ninja -DCMAKE_BUILD_TYPE=Release"
if not defined CACHED_GEN if defined NINJA set "CACHED_GEN=Ninja (new)"
echo [2/4] Generator: !CACHED_GEN!
echo        Qt     : !QT_DIR!
echo        fio    : !FIO_DIR!
echo        cmake  : !CMAKE!

rem ---------------------------------------------------------------- configure
echo [3/4] CMake configure ...
if not exist "!BUILD_DIR!" mkdir "!BUILD_DIR!"
"!CMAKE!" -S "!ROOT!" -B "!BUILD_DIR!" !GEN_ARGS! -DCMAKE_PREFIX_PATH="!QT_DIR!" -DFIO_DIR="!FIO_DIR!"
if errorlevel 1 (
    echo.
    echo [ERROR] CMake configure failed.
    echo         If it complains about the generator, run:  build.bat clean
    exit /b 1
)

rem ---------------------------------------------------------------- build
echo [4/4] Building Release (jobs=!JOBS!) ...
"!CMAKE!" --build "!BUILD_DIR!" --config Release --parallel !JOBS!
if errorlevel 1 (
    echo.
    echo [ERROR] Build failed.
    exit /b 1
)

rem Ninja is single-config: output lands in build\. VS generator: build\Release\.
set "EXE=!BUILD_DIR!\Release\qtfiogui.exe"
if not exist "!EXE!" set "EXE=!BUILD_DIR!\qtfiogui.exe"
if not exist "!EXE!" (
    echo [ERROR] Build finished but qtfiogui.exe was not found.
    exit /b 1
)
echo.
echo Built: !EXE!

rem ---------------------------------------------------------------- actions
if /i "!ACTION!"=="package" (
    echo.
    echo [package] Building distributable ...
    "!CMAKE!" --build "!BUILD_DIR!" --config Release --target package
    if errorlevel 1 exit /b 1
    echo.
    call :verify_dist
    if errorlevel 1 (
        echo [ERROR] Package is missing files that the license requires.
        exit /b 1
    )
    echo Packaged: !DIST_DIR!
)

if /i "!ACTION!"=="run" (
    set "PATH=!QT_DIR!\bin;!PATH!"
    start "" "!EXE!"
)

if /i "!ACTION!"=="selftest" (
    if "!SELFTEST_TARGET!"=="" (
        echo.
        echo [ERROR] selftest needs a target volume, e.g.:  build.bat selftest /target G:
        echo         Warning: it really writes data on that volume. Not your system disk!
        exit /b 1
    )
    echo.
    echo [selftest] target=!SELFTEST_TARGET!  fill-limit=!FILL_LIMIT! MiB
    set "PATH=!QT_DIR!\bin;!PATH!"
    "!EXE!" --selftest --fio "!FIO_DIR!\fio.exe" --outdir "!BUILD_DIR!\selftest" --selftest-target !SELFTEST_TARGET! --selftest-fill-limit !FILL_LIMIT!
    echo Report: !BUILD_DIR!\selftest\selftest.log
)

echo.
echo Done.
call :restore_cp
endlocal
exit /b 0

rem ============================================================================
rem  do_clean -- wipe build\ only
rem ============================================================================
:do_clean
echo [clean] Removing !BUILD_DIR!
if exist "!BUILD_DIR!" rmdir /s /q "!BUILD_DIR!"
echo Done.
call :restore_cp
endlocal
exit /b 0

rem ============================================================================
rem  do_headers -- make sure every source file carries the GPL notice
rem
rem  The license says the program is GPL-3.0-or-later; a source file without a
rem  copyright line is, to anyone who copies that single file out of the repo,
rem  unlicensed. tools\add_license_headers.py is idempotent, so this is a safe
rem  thing to run any time.
rem ============================================================================
:do_headers
set "PY="
for %%i in (python.exe) do if not "%%~$PATH:i"=="" set "PY=%%~$PATH:i"
if not defined PY (
    echo [ERROR] python.exe not found on PATH ^(needed to stamp headers^).
    exit /b 1
)
echo [headers] Checking source files ...
"!PY!" "!ROOT!\tools\add_license_headers.py" --check
if errorlevel 1 (
    echo.
    echo [headers] Adding the missing notices ...
    "!PY!" "!ROOT!\tools\add_license_headers.py"
    if errorlevel 1 exit /b 1
)
echo Done.
call :restore_cp
endlocal
exit /b 0

rem ============================================================================
rem  do_check -- repo hygiene before a commit
rem ============================================================================
:do_check
set "FAILED="
echo [check] License files ...
call :need "LICENSE"
call :need "THIRD-PARTY-NOTICES.md"
call :need "licenses\Qt-LGPLv3.txt"
call :need "licenses\fio-GPLv2.txt"
echo [check] fio runtime and its own license ...
call :need "third_party\fio\fio.exe"
call :need "third_party\fio\COPYING.txt"
echo [check] Source headers ...
set "PY="
for %%i in (python.exe) do if not "%%~$PATH:i"=="" set "PY=%%~$PATH:i"
if defined PY (
    "!PY!" "!ROOT!\tools\add_license_headers.py" --check >nul 2>&1
    if errorlevel 1 (
        echo    MISSING: run  build.bat headers  to stamp them
        set "FAILED=1"
    ) else (
        echo    OK
    )
) else (
    echo    SKIP ^(python.exe not on PATH^)
)
echo [check] Stray test results ^(should be git-ignored, not committed^) ...
set "STRAY="
for %%f in ("!ROOT!\ml_disktest-*.csv") do if exist "%%~f" set "STRAY=1"
if defined STRAY (
    echo    NOTE: ml_disktest-*.csv found in the repo root
    echo          .gitignore covers them; make sure they are not staged
)
echo.
if defined FAILED (
    echo [check] FAILED
    call :restore_cp
    endlocal
    exit /b 1
)
echo [check] All good.
call :restore_cp
endlocal
exit /b 0

rem ============================================================================
rem  do_git -- initialize the repository and make the first commit
rem
rem  Deliberately does NOT push anywhere and does NOT run "git add -A" blindly:
rem  it prints exactly what it would commit first. Re-running after the first
rem  commit just stages and commits whatever changed.
rem ============================================================================
:do_git
where git >nul 2>&1
if errorlevel 1 (
    echo [ERROR] git not found on PATH.
    exit /b 1
)
if not exist "!ROOT!\.git" (
    echo [git] Initializing repository in !ROOT! ...
    git -C "!ROOT!" init
    if errorlevel 1 exit /b 1
    git -C "!ROOT!" branch -M main
)
echo.
echo [git] Files that would be committed:
echo ---------------------------------------------------------------
git -C "!ROOT!" add -A --dry-run
echo ---------------------------------------------------------------
echo.
echo [git] Staging and committing ...
git -C "!ROOT!" add -A
if errorlevel 1 exit /b 1
set "MSG=!GIT_MSG!"
if "!MSG!"=="" set "MSG=Initial commit"
git -C "!ROOT!" commit -m "!MSG!"
if errorlevel 1 (
    echo.
    echo [git] Nothing to commit, or the commit failed.
    echo       If git asked you to configure user.name / user.email, do that
    echo       first, then re-run:  build.bat git
    call :restore_cp
    endlocal
    exit /b 1
)
echo.
echo [git] Done.  Current state:
git -C "!ROOT!" --no-pager log --oneline -3
git -C "!ROOT!" status --short
call :restore_cp
endlocal
exit /b 0

rem ---------------------------------------------------------------- usage
:usage_ok
call :print_usage
call :restore_cp
endlocal
exit /b 0

:usage_err
call :print_usage
call :restore_cp
endlocal
exit /b 2

:print_usage
echo.
echo  qtfiogui - Disk Reliability Tester : build script
echo.
echo  Usage:  build.bat [action] [options]
echo.
echo  Build actions (default: build):
echo    build       wipe build\, configure, then build Release
echo    package     build, then package into build\dist (portable)
echo    selftest    build, then run an unattended selftest (needs /target)
echo    run         build, then launch the program
echo    clean       only wipe build\ (no configure, no build)
echo.
echo  Repo actions (no compiler or Qt needed):
echo    headers     add the GPL notice to any source file missing it
echo    check       verify licenses, fio, and source headers
echo    git         init the repo and commit (asks nothing, pushes nothing)
echo    help        this text
echo.
echo  Note: build/package/selftest/run all rebuild from scratch
echo        (build\ is removed first) because incremental builds
echo        were unreliable here.
echo.
echo  Options:
echo    /qt ^<dir^>        Qt directory, e.g. C:\Qt\6.8.3\msvc2022_64
echo                      (or set the QT_DIR environment variable)
echo    /vs ^<dir^>        Visual Studio install dir (default: found via vswhere)
echo    /target ^<vol^>    selftest target volume, e.g. G: or G:/
echo    /filllimit ^<MiB^> selftest fill limit, default 1000 (filling a real
echo                      disk takes forever)
echo    /jobs ^<n^>         parallel compile jobs, default = CPU count
echo    /message ^<text^>  commit message for the git action
echo.
echo  Examples:
echo    build.bat                                    just build
echo    build.bat clean                              start over
echo    build.bat package                            build + package
echo    build.bat check                              repo hygiene check
echo    build.bat git /message "first commit"        init + commit
echo    build.bat selftest /target G: /filllimit 3000
echo    build.bat /qt C:\Qt\6.8.3\msvc2022_64        pick Qt explicitly
echo.
echo  Note: cmd splits arguments on "=", so /qt=X and /qt X are equivalent.
echo.
echo  Requirements:
echo   - Visual Studio with the "Desktop development with C++" workload
echo   - Qt 6.5+ (Widgets + Charts), MSVC x64 build
echo   - CMake 3.21+
echo   - Python 3 (only for the icon target and the headers action)
echo.
exit /b 0

rem ============================================================================
rem  helpers
rem ============================================================================
rem :need <relative path> -- fail the check when the file is absent
:need
if exist "!ROOT!\%~1" (
    echo    OK      %~1
) else (
    echo    MISSING %~1
    set "FAILED=1"
)
exit /b 0

rem :verify_dist -- a portable package without these is not redistributable
:verify_dist
set "DIST_BAD="
if not exist "!DIST_DIR!\licenses\LICENSE"                set "DIST_BAD=1"
if not exist "!DIST_DIR!\licenses\THIRD-PARTY-NOTICES.md" set "DIST_BAD=1"
if not exist "!DIST_DIR!\licenses\Qt-LGPLv3.txt"          set "DIST_BAD=1"
if not exist "!DIST_DIR!\licenses\fio-GPLv2.txt"          set "DIST_BAD=1"
if not exist "!DIST_DIR!\fio\fio.exe"                     set "DIST_BAD=1"
if not exist "!DIST_DIR!\fio\COPYING.txt"                 set "DIST_BAD=1"
if defined DIST_BAD (
    echo [verify] dist is missing required license/runtime files:
    if not exist "!DIST_DIR!\licenses\LICENSE"                echo          licenses\LICENSE
    if not exist "!DIST_DIR!\licenses\THIRD-PARTY-NOTICES.md" echo          licenses\THIRD-PARTY-NOTICES.md
    if not exist "!DIST_DIR!\licenses\Qt-LGPLv3.txt"          echo          licenses\Qt-LGPLv3.txt
    if not exist "!DIST_DIR!\licenses\fio-GPLv2.txt"          echo          licenses\fio-GPLv2.txt
    if not exist "!DIST_DIR!\fio\fio.exe"                     echo          fio\fio.exe
    if not exist "!DIST_DIR!\fio\COPYING.txt"                 echo          fio\COPYING.txt
    exit /b 1
)
echo [verify] dist is complete ^(exe + Qt + fio + licenses^)
exit /b 0

:restore_cp
if defined OLDCP chcp !OLDCP! >nul 2>&1
exit /b 0
