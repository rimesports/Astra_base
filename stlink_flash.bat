@echo off
rem stlink_flash.bat — flash STM32F411 Black Pill via ST-Link
rem Usage: stlink_flash.bat <firmware.elf>
rem Requires NRST wired: ST-Link RST -> Black Pill left header pin 16

set OPENOCD=C:\Users\yegen\.platformio\packages\tool-openocd\bin\openocd.exe
set SCRIPTS=C:\Users\yegen\.platformio\packages\tool-openocd\openocd\scripts
set CFG=C:\Users\yegen\Robotics\STM32\blackpill_stlink_upload.cfg

"%OPENOCD%" -s "%SCRIPTS%" -c "set FIRMWARE {%1}" -f "%CFG%"
