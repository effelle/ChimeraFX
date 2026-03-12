@echo off
echo Installing requirements...
pip install -r requirements.txt
echo.
echo Starting ChimeraFX Visualizer...
echo Usage: run.bat --rgbw (for 4-byte SK6812) or run.bat (for 3-byte WS2812)
python visualizer.py %*
pause
