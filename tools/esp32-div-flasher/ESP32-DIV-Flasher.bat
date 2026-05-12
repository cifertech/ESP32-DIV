@echo off
REM Launches the flasher with Python. For a standalone .exe with icon, run build_exe.bat.
setlocal
set "HERE=%~dp0"
cd /d "%HERE%" || exit /b 1

where python >nul 2>&1 && (
  python "%HERE%flash_div.py" %*
  exit /b %ERRORLEVEL%
)
where py >nul 2>&1 && (
  py -3 "%HERE%flash_div.py" %*
  exit /b %ERRORLEVEL%
)

echo Python 3 was not found on PATH. Install Python from python.org or the Microsoft Store,
echo then run:  pip install -r "%HERE%requirements.txt"
pause
exit /b 1
