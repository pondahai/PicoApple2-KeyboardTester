@echo off
setlocal enabledelayedexpansion
title KeyboardTester Offset Build

:: ===========================================================================
:: build_offset.bat - offset build + trampoline merge for rp2040-retro-loader
::
:: NOTE: keep this file pure ASCII. cmd.exe parses .bat with the system
:: codepage (cp950 here); UTF-8 Chinese in comments corrupts the surrounding
:: syntax. The Chinese write-up lives in README section "3. / 4." and in
:: loader_offset/*.py.
::
:: Unlike PicoApple2 there is no Rust core and no generated library here, so
:: this is just: linker script -> compile -> layout check -> merge.
::
:: Usage:
::     build_offset.bat                  loader repo defaults to ..\rp2040-retro-loader
::     build_offset.bat <loader repo path>
::
:: Outputs (all under build_offset\):
::     KeyboardTester.ino.uf2          body only, first 16KB empty - NOT flashable alone
::     KeyboardTester_standalone.uf2   trampoline + body - this is the one to use
::     KeyboardTester_standalone.RAW   menu icon, renamed to match the uf2
::
:: This script does not flash. The standalone file works both ways: drop it
:: in the SD card root for the loader, or picotool load -v -x it directly.
:: ===========================================================================

set "SCRIPT_DIR=%~dp0"
set "PROJECT_ROOT=%SCRIPT_DIR%"
set "OUT_DIR=%PROJECT_ROOT%build_offset"
set "OFFSET_LD=%PROJECT_ROOT%loader_offset\memmap_app_arduino.ld"
set "FQBN=rp2040:rp2040:rpipico"

:: --- locate arduino-cli ---------------------------------------------------
:: Override by setting ARDUINO_CLI_PATH before calling this script. Otherwise
:: try the Arduino IDE 2.x bundled copy, then PATH.
if not "%ARDUINO_CLI_PATH%"=="" goto :cli_done
set "ARDUINO_CLI_PATH=C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
if exist "%ARDUINO_CLI_PATH%" goto :cli_done
set "ARDUINO_CLI_PATH=%LOCALAPPDATA%\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
if exist "%ARDUINO_CLI_PATH%" goto :cli_done
for /f "delims=" %%i in ('where arduino-cli 2^>nul') do set "ARDUINO_CLI_PATH=%%i"
:cli_done
if not exist "%ARDUINO_CLI_PATH%" (
    echo [ERROR] arduino-cli not found.
    echo         Set ARDUINO_CLI_PATH yourself, or install Arduino IDE 2.x.
    pause & exit /b 1
)
echo Arduino CLI: %ARDUINO_CLI_PATH%

:: --- locate the loader repo ----------------------------------------------
if "%~1"=="" (
    set "LOADER_PATH=%PROJECT_ROOT%..\rp2040-retro-loader"
) else (
    set "LOADER_PATH=%~1"
)
if not exist "!LOADER_PATH!\tools\merge_uf2.py" (
    echo [ERROR] rp2040-retro-loader not found at "!LOADER_PATH!"
    echo         Usage: build_offset.bat ^<loader repo path^>
    pause & exit /b 1
)

echo.
echo ========================================================
echo [1/4] Generating offset linker script...
echo ========================================================
:: Regenerated from YOUR installed arduino-pico version - the stock
:: memmap_default.ld differs between releases, so a stale template silently
:: links against the wrong flash layout.
python "%PROJECT_ROOT%loader_offset\gen_app_ld.py"
if %errorlevel% neq 0 ( echo [ERROR] gen_app_ld.py failed. & pause & exit /b 1 )

echo.
echo ========================================================
echo [2/4] Compiling sketch (OFFSET, link at 0x10004000)...
echo ========================================================
:: recipe.hooks.linking.prelink.1.pattern
::     arduino-pico generates its linker script at build time: simplesub.py
::     substitutes __FLASH_LENGTH__ etc. into lib/rp2040/memmap_default.ld and
::     writes {build.path}/memmap_default.ld, which the link recipe then reads
::     by that hardcoded name. So the way to swap linker scripts is NOT to add
::     -Wl,--script (it would fight the hardcoded one) but to repoint this
::     hook's --input at our offset template. Every other argument is copied
::     verbatim from platform.txt.
"%ARDUINO_CLI_PATH%" compile --fqbn %FQBN% ^
    --build-property "recipe.hooks.linking.prelink.1.pattern=\"{runtime.tools.pqt-python3.path}/python3\" -I \"{runtime.platform.path}/tools/simplesub.py\" --input \"%OFFSET_LD%\" --out \"{build.path}/memmap_default.ld\" --sub __FLASH_LENGTH__ {build.flash_length} --sub __EEPROM_START__ {build.eeprom_start} --sub __FS_START__ {build.fs_start} --sub __FS_END__ {build.fs_end} --sub __RAM_LENGTH__ {build.ram_length} --sub __PSRAM_LENGTH__ {build.psram_length}" ^
    --output-dir "%OUT_DIR%" "%PROJECT_ROOT%KeyboardTester"
if %errorlevel% neq 0 ( echo [ERROR] Arduino build failed. & pause & exit /b 1 )

echo.
echo ========================================================
echo [3/4] Checking flash layout...
echo ========================================================
:: Validates the BARE offset body. Do not run this on the merged standalone
:: file - that one starts at 0x10000000 by design and will be reported as an
:: error.
python "%PROJECT_ROOT%loader_offset\check_flash_layout.py" "%OUT_DIR%\KeyboardTester.ino.uf2"
if %errorlevel% neq 0 ( echo [ERROR] Layout check failed - do not flash this file. & pause & exit /b 1 )

echo.
echo ========================================================
echo [4/4] Merging with trampoline...
echo ========================================================
:: The bare body has no .boot2 (gen_app_ld.py drops it) and the RP2040 boot
:: ROM only looks at the first 256 bytes of flash, so the body alone cannot
:: boot over USB. The trampoline supplies that first 16KB. The loader skips
:: every block below 0x10004000 when flashing from SD, so the same merged
:: file is safe on both paths.
if not exist "!LOADER_PATH!\build\trampoline.uf2" (
    echo [ERROR] "!LOADER_PATH!\build\trampoline.uf2" not found.
    echo         Build the loader once first - see its README section 2.2.
    pause & exit /b 1
)
python "!LOADER_PATH!\tools\merge_uf2.py" "!LOADER_PATH!\build\trampoline.uf2" ^
    "%OUT_DIR%\KeyboardTester.ino.uf2" -o "%OUT_DIR%\KeyboardTester_standalone.uf2"
if %errorlevel% neq 0 ( echo [ERROR] merge_uf2.py failed. & pause & exit /b 1 )

:: The loader pairs an icon with a uf2 by filename: strip .uf2, append .RAW.
:: Copy the asset under the standalone name so both land on the SD card
:: matching each other.
if exist "%PROJECT_ROOT%assets\KeyboardTester.ino.RAW" (
    copy /y "%PROJECT_ROOT%assets\KeyboardTester.ino.RAW" "%OUT_DIR%\KeyboardTester_standalone.RAW" >nul
    echo [OK] Menu icon copied as KeyboardTester_standalone.RAW
)

echo.
echo ========================================================
echo SUCCESS
echo ========================================================
echo   %OUT_DIR%\KeyboardTester_standalone.uf2
echo.
echo   Put it in the SD card root for the loader (with the .RAW next to it),
echo   or flash it directly:
echo     picotool load -v -x "%OUT_DIR%\KeyboardTester_standalone.uf2"
echo.
echo   Note: picotool load -x is a soft reset, so the loader passes straight
echo   through to the app without showing its menu. To see the menu, flash
echo   without -x and then power-cycle.
echo ========================================================
pause
