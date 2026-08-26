@echo off
setlocal

if not defined CEDEV set "CEDEV=%~dp0..\CEDev"

if not exist "%CEDEV%\meta\makefile.mk" (
    echo CEDEV toolchain not found at "%CEDEV%".
    echo Set the CEDEV environment variable to your CEdev install and try again.
    exit /b 1
)

set "PATH=%CEDEV%\bin;%PATH%"

make %*