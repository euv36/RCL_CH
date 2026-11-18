#!/usr/bin/env python3
import subprocess
for ctrl in ["auto_exposure=3", "exposure_time_absolute=157", "gain=0",
             "brightness=0", "exposure_dynamic_framerate=1",
             "backlight_compensation=1"]:
    r = subprocess.run(["v4l2-ctl", "-d", "/dev/video0", "-c", ctrl],
                       capture_output=True, text=True)
    print(ctrl, "->", r.returncode, (r.stdout + r.stderr).strip())
