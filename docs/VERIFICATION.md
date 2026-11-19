# Verification report

## Task 12 — temporary Pi smoke procedure

Status: `[UNVERIFIED ON HARDWARE]`. Pi SSH/rsync commands and the live ZMQ smoke were **not executed** in this session (blind Pi mode).

Static checks performed locally:

```powershell
& 'C:\msys64\usr\bin\bash.exe' -n scripts/deploy_pi_temp.sh
& 'C:\msys64\usr\bin\bash.exe' -n scripts/stop_pi_temp.sh
python -m py_compile tests/integration/pi_service_smoke.py
git diff --check
```

Result: all commands exited 0; generated `__pycache__/` removed and ignored.

Not verified until a human explicitly authorizes Pi access:

- build on Pi with system OpenCV/ZMQ;
- ports 5555/5557/5559 availability and temporary service start;
- multipart JPEG/JSON capture for 5 seconds;
- CAM SET application and config checksum restoration;
- port closure after `scripts/stop_pi_temp.sh`.

Local loopback coverage of the same wire contract already passed in Task 11 (`pi_zmq_loopback`, 127.0.0.1:15555/15557/15559, fake V4L2), but it is not a hardware proof.
