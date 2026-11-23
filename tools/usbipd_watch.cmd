@echo off
setlocal EnableExtensions EnableDelayedExpansion
set "USBIPD=%~1"
set "LOCK=%~2"
if "!USBIPD!"=="" exit /b 0
if "!LOCK!"=="" exit /b 0

:loop
if not exist "!LOCK!" exit /b 0
for /f "usebackq delims=" %%L in (`"!USBIPD!" list 2^>nul`) do (
    set "_LINE=%%L"
    if "!_LINE:Attached=!"=="!_LINE!" (
        for /f "tokens=1,2" %%A in ("%%L") do (
            set "_VP=%%B"
            set "_HIT=0"
            if /i "!_VP!"=="054c:0ca8" set "_HIT=1"
            if /i "!_VP!"=="054c:0d12" set "_HIT=1"
            if /i "!_VP!"=="054c:0336" set "_HIT=1"
            if "!_HIT!"=="1" (
                if not "!_LINE:Not shared=!"=="!_LINE!" (
                    "!USBIPD!" bind --force --busid %%A >nul 2>&1
                )
                "!USBIPD!" attach --wsl --busid %%A >nul 2>&1
            )
        )
    )
)
ping -n 2 127.0.0.1 >nul
goto :loop
