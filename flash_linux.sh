#!/bin/bash
echo "================================================================================"
echo "             ESP32-DIV FENRIR v2.0 - Linux Flash Script"
echo "================================================================================"
echo ""

# Find available ports
echo "Available ports:"
ls /dev/ttyUSB* 2>/dev/null || ls /dev/ttyACM* 2>/dev/null || echo "No USB devices found"
echo ""

read -p "Enter port (e.g., /dev/ttyUSB0): " PORT

echo ""
echo "Flashing to $PORT..."
echo ""

esptool.py --chip esp32 --port "$PORT" --baud 921600 write_flash \
    0x1000 bootloader.bin \
    0x8000 partitions.bin \
    0xe000 boot_app0.bin \
    0x10000 ESP32-DIV.bin

echo ""
echo "================================================================================"
echo "Flash complete! Press RESET on your device or unplug/replug USB."
echo "================================================================================"
