@echo off
setlocal EnableExtensions
cd /d "%~dp0" || exit /b 1

python restore_icon_assets.py
if not exist "assets\app_icon.ico" (
  echo Missing assets\app_icon.ico — run: python restore_icon_assets.py
  exit /b 1
)

pip show pyinstaller >nul 2>&1 || pip install pyinstaller

set "EXTRA="
if exist "bundled\" set "EXTRA=--add-data bundled;bundled"

pyinstaller --noconfirm --clean --onefile --windowed --name ESP32-DIV-Flasher --icon assets\app_icon.ico --add-data "assets;assets" %EXTRA% --collect-all esptool --hidden-import serial --hidden-import serial.tools.list_ports flash_div.py

echo.
echo Output: dist\ESP32-DIV-Flasher.exe  ^(windowed GUI; for CLI with console use: python flash_div.py ...^)
exit /b 0
