@echo off
setlocal

cmake -S . -B build -G Ninja
if errorlevel 1 (
    exit /b %errorlevel%
)

cmake --build build --target chess_onefile
if errorlevel 1 (
    exit /b %errorlevel%
)

echo.
echo One-file package created:
echo   build\release\Chess-OneFile.exe

endlocal
