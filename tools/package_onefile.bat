@echo off
setlocal
pushd "%~dp0.." || exit /b 1

set "CHESS_RELEASE_VERSION=%~1"

if not "%CHESS_RELEASE_VERSION%"=="" (
    set "CHESS_CMAKE_VERSION_ARG=-DCHESS_RELEASE_VERSION=%CHESS_RELEASE_VERSION%"
) else (
    set "CHESS_CMAKE_VERSION_ARG="
)

cmake -S . -B build-release-windows -G Ninja -DCMAKE_BUILD_TYPE=Release -DCHESS_WINDOWS_PORTABLE_RUNTIME=ON %CHESS_CMAKE_VERSION_ARG%
if errorlevel 1 goto failure

cmake --build build-release-windows
if errorlevel 1 goto failure
ctest --test-dir build-release-windows --output-on-failure
if errorlevel 1 goto failure
cmake --build build-release-windows --target chess_onefile
if errorlevel 1 goto failure

echo.
echo Windows installer created:
echo   build-release-windows\release\chess.exe
echo   build-release-windows\release\chess.exe.sha256
echo See the release directory for the versioned, architecture-specific filename.

popd
exit /b 0

:failure
set "CHESS_EXIT_CODE=%errorlevel%"
popd
exit /b %CHESS_EXIT_CODE%
