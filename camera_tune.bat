@echo off
rem OLD fisheye/gate tuner (fisheye_test.py) - kept for fish-eye and
rem per-gate HSV tuning experiments. The main app is camera.bat.
cd /d "%~dp0"
echo Starting the fisheye tuner (old tool)...
echo.
python -u UniKostyl\fisheye_test.py
echo.
echo Tuner closed.
pause
