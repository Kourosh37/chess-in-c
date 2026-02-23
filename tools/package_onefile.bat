@echo off
setlocal

set "CHESS_RELEASE_VERSION=%~1"

if not "%CHESS_RELEASE_VERSION%"=="" (
    set "CHESS_CMAKE_VERSION_ARG=-DCHESS_RELEASE_VERSION=%CHESS_RELEASE_VERSION%"
) else (
    set "CHESS_CMAKE_VERSION_ARG="
)

cmake -S . -B build -G Ninja %CHESS_CMAKE_VERSION_ARG%
if errorlevel 1 (
    exit /b %errorlevel%
)

cmake --build build --target chess_onefile
if errorlevel 1 (
    exit /b %errorlevel%
)

echo.
echo One-file package created:
if not "%CHESS_RELEASE_VERSION%"=="" (
    echo   build\release\chess-windows-x64-v%CHESS_RELEASE_VERSION%.exe
    echo   build\release\chess-windows-x64-v%CHESS_RELEASE_VERSION%.exe.sha256
) else (
    echo   build\release\chess.exe
    echo   build\release\chess.exe.sha256
)
echo   build\release\chess.exe

endlocal
