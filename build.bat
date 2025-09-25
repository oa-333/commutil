@echo off
setlocal enabledelayedexpansion enableextensions

REM Possible command line options:
REM -v|--verbose
REM -d|--debug
REM -r|--release
REM -w|--rel-with-debug-info
REM -c|--conn sqlite|mysql|postgresql|kafka
REM -i|--install-dir <INSTALL_DIR>
REM -c|--clean
REM -r|--rebuild (no reconfigure)
REM -g|--reconfigure
REM -a|--clang

REM set default values
SET PLATFORM=WINDOWS
SET BUILD_TYPE=Debug
SET INSTALL_DIR=C:\install\commutil
SET VERBOSE=0
SET FULL=0
SET CLEAN=0
SET REBUILD=0
SET RE_CONFIG=0
SET CLANG=0

echo [DEBUG] Parsing args
:GET_OPTS
SET ARG1=%1
SET ARG1=%ARG1: =%
SET ARG2=%2
SET ARG2=%ARG2: =%
echo [DEBUG] processing option "%1" "%2"
IF /I "%1" == "-v" SET VERBOSE=1 & GOTO CHECK_OPTS
IF /I "%1" == "--verbose" SET VERBOSE=1 & GOTO CHECK_OPTS
IF /I "%1" == "-d" SET BUILD_TYPE=Debug & GOTO CHECK_OPTS
IF /I "%1" == "--debug" SET BUILD_TYPE=Debug & GOTO CHECK_OPTS
IF /I "%1" == "-r" SET BUILD_TYPE=Release & GOTO CHECK_OPTS
IF /I "%1" == "--release" SET BUILD_TYPE=Release & GOTO CHECK_OPTS
IF /I "%1" == "-w" SET BUILD_TYPE=RelWithDebInfo & GOTO CHECK_OPTS
IF /I "%1" == "--rel-with-debug-info" SET BUILD_TYPE=RelWithDebInfo & GOTO CHECK_OPTS
IF /I "%1" == "-i" SET INSTALL_DIR=%2 & shift & GOTO CHECK_OPTS
IF /I "%1" == "--install-dir" SET INSTALL_DIR=%2 & shift & GOTO CHECK_OPTS
IF /I "%1" == "-c" SET CLEAN=1 & GOTO CHECK_OPTS
IF /I "%1" == "--clean" SET CLEAN=1 & GOTO CHECK_OPTS
IF /I "%1" == "-r" SET REBUILD=1 & SET CLEAN=1 & GOTO CHECK_OPTS
IF /I "%1" == "--rebuild" SET REBUILD=1 & SET CLEAN=1 & GOTO CHECK_OPTS
IF /I "%1" == "-g" SET RE_CONFIG=1 & SET REBUILD=1 & SET CLEAN=1 & GOTO CHECK_OPTS
IF /I "%1" == "--reconfigure" SET RE_CONFIG=1 & SET REBUILD=1  & SET CLEAN=1 & GOTO CHECK_OPTS
IF /I "%1" == "-a" SET CLANG=1 & GOTO CHECK_OPTS
IF /I "%1" == "--clang" SET CLANG=1 & GOTO CHECK_OPTS

REM handle invalid option
IF NOT "%1" == "" (
    echo [ERROR] Invalid option: "%1"
    GOTO HANDLE_ERROR
)

:CHECK_OPTS
shift
IF "%1" == "--" shift && GOTO SET_OPTS
IF NOT "%1" == "" GOTO GET_OPTS

:SET_OPTS
echo [DEBUG] Parsed args:
echo [DEBUG] BUILD_TYPE=%BUILD_TYPE%
echo [DEBUG] INSTALL_DIR=%INSTALL_DIR%
echo [DEBUG] VERBOSE=%VERBOSE%
echo [DEBUG] CLEAN=%CLEAN%
echo [DEBUG] REBUILD=%REBUILD%
echo [DEBUG] RE_CONFIG=%RE_CONFIG%
echo [DEBUG] CLANG=%CLANG%
echo [DEBUG] Args parsed, options left: %*

REM set normal options
echo [INFO] Build type: %BUILD_TYPE%
echo [INFO] Install dir: %INSTALL_DIR%
SET OPTS=-DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DCMAKE_INSTALL_PREFIX=%INSTALL_DIR%
IF %VERBOSE% EQU 1 SET OPTS=%OPTS% -DCMAKE_VERBOSE_MAKEFILE=ON
echo [DEBUG] Current options: %OPTS%

REM vcpkg
set VCPKG_ROOT=C:\Oren\SWE\dev\tools\vcpkg
set PATH=%VCPKG_ROOT%;%PATH%
vcpkg add port gzip-hpp
vcpkg add port zlib
vcpkg add port libuv

REM TODO: temporary workaround, something with presets is not working
set ZLIB_BASE=%VCPKG_ROOT%\packages\zlib_x64-windows
SET OPTS=-DZLIB_INCLUDE_DIR="%ZLIB_BASE%\include" -DZLIB_LIBRARY="%ZLIB_BASE%\lib\zlib.lib" %OPTS%

REM prepare build directory
SET BUILD_DIR=cmake_build\%PLATFORM%-%BUILD_TYPE%
echo [INFO] Using build directory: %BUILD_DIR%
if NOT EXIST %BUILD_DIR% mkdir %BUILD_DIR%
if errorlevel 1 (
    echo [ERROR] failed to create build directory %BUILD_DIR%
    goto HANDLE_ERROR
)
IF %CLANG% EQU 1 SET CXX=clang-cl

pushd %BUILD_DIR% > NUL

REM print cmake info
echo [INFO] CMake version:
cmake --version

if %VERBOSE% EQU 1 (
    SET VERBOSE_OPT=--verbose
)

IF %CLEAN% EQU 1 (
    echo [INFO] Running target clean
    cmake --build . -j %VERBOSE_OPT% --target clean
    if errorlevel 1 (
        echo [ERROR] Clean failed, see errors above, aborting
        popd > NUL
        GOTO HANDLE_ERROR
    )
    echo [INFO] Clean DONE
    IF %REBUILD% EQU 0 (
        popd > NUL
        exit /b 0
    )
)

REM Run fresh if re-configure is requested
IF %RE_CONFIG% EQU 1 (
    echo [INFO] Forcing fresh configuration
    SET OPTS=--fresh %OPTS%
)

REM configure phase
echo [INFO] Configuring project
echo [INFO] Executing build command cmake %OPTS% ..\..\ -G "Ninja"
cmake %OPTS% ..\..\ -G "Ninja"
if errorlevel 1 (
    echo [ERROR] Configure phase failed, see errors above, aborting
    popd > NUL
    goto HANDLE_ERROR
)

REM build phase
REM NOTE: On windows MSVC (multi-config system), the configuration to build should be specified in build command
echo [INFO] Building
echo [INFO] Executing command: cmake --build . -j %VERBOSE_OPT% --config %BUILD_TYPE%
cmake --build . -j %VERBOSE_OPT% --config %BUILD_TYPE%
if errorlevel 1 (
    echo [ERROR] Build phase failed, see errors above, aborting
    popd > NUL
    goto HANDLE_ERROR
)

REM install phase
echo [INFO] Installing
cmake --install . %VERBOSE_OPT% --config %BUILD_TYPE%
if errorlevel 1 (
    echo [ERROR] Install phase failed, see errors above, aborting
    popd > NUL
    goto HANDLE_ERROR
)

popd > NUL
exit /b 0

:HANDLE_ERROR
echo [ERROR] Build failed, see errors above, aborting
exit /b 1