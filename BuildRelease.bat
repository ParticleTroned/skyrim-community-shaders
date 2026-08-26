@echo off

setlocal

set preset="ALL"
if NOT "%1" == "" (
    set preset=%1
)

echo Running preset %preset%

set "TOOL_TEMP=%~dp0build\tool-temp"
if not exist "%TOOL_TEMP%" (
    mkdir "%TOOL_TEMP%"
    if errorlevel 1 exit /b 1
)
set "TEMP=%TOOL_TEMP%"
set "TMP=%TOOL_TEMP%"

cmake.exe -S . --preset=%preset% --check-stamp-file "build\%preset%\CMakeFiles\generate.stamp"
if %ERRORLEVEL% NEQ 0 exit /b 1
cmake.exe --build --preset=%preset%
if %ERRORLEVEL% NEQ 0 exit /b 1

endlocal
