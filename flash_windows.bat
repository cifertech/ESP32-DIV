@echo off
echo ================================================================================
echo              ESP32-DIV FENRIR v2.0 - Windows Flash Script
echo ================================================================================
echo.

set /p PORT="Enter COM port (e.g., COM3): "

echo.
echo Flashing to %PORT%...
echo.

esptool.py --chip esp32 --port %PORT% --baud 921600 write_flash 0x1000 bootloader.bin 0x8000 partitions.bin 0xe000 boot_app0.bin 0x10000 ESP32-DIV.bin

echo.
echo ================================================================================
echo Flash complete! Press RESET on your device or unplug/replug USB.
echo ================================================================================
pause
