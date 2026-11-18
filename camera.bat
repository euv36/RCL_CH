@echo off
rem Main camera app (cv_kostyl): pygame UI with the gate status panel,
rem radar, CAM SET window (exposure etc.), saved gate colors.
rem The ELP is auto-detected by the "can do 1920x1080" trait
rem (see UniKostyl/README_CAMERA.md).
cd /d "%~dp0"
echo Starting the camera app (auto-detect ELP, full FOV 1920x1080)...
echo.
python -u UniKostyl\cv_kostyl.py
echo.
echo Camera app closed.
pause
