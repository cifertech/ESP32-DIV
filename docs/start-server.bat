@echo off
cd /d "%~dp0"
echo Starting site at http://localhost:5500
echo Press Ctrl+C to stop.
npx --yes serve . -l 5500
