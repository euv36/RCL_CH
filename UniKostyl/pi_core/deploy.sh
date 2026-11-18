#!/usr/bin/env bash
# deploy.sh — copy pi_core to the Pi, build, run unit tests and smoke tests.
# Run from Git Bash on the laptop, from the repo root or this directory.
#
# Usage (password comes from the user — never hardcode it):
#   export PI_HOST=<pi-ip>        # find via `arp -a | grep d8:3a:dd`
#   export PI_USER=twox
#   export PI_PW=<password>
#   export LAPTOP_IP=<laptop-ip>  # optional; `ipconfig` on the laptop
#   bash UniKostyl/pi_core/deploy.sh            # everything up to smoke
#   ALLOW_APT=1 bash UniKostyl/pi_core/deploy.sh  # also apt install (sudo)
#   SKIP_DEPLOY=1 ...                           # reuse already-copied sources
#
# Expected numbers (measured for the Python original, UniKostyl/AGENTS.md —
# the C++ core must match or beat them):
#   --probe:      "Фактический режим: 1920x1080 @ 30.0 fps, FOURCC=MJPG, raw=True"
#                 "Сырой захват: ~26 fps"; decode reduced_2 ~15-16 ms @ 1 thread
#   --duration 12: processing ~25.9 fps (camera ceiling), capture ~26 fps,
#                 stages: decode ~23.5 ms (12-frame pipeline incl. resize),
#                 hsv ~1.1 ms, gates ~11.3 ms for 3 gates
#   --print-serial: lines match ^G[0-9],[-0-9]+,+$ / ^G[0-9],-1,0$ / ^HB$
set -euo pipefail

PI_HOST="${PI_HOST:-10.192.226.51}"
PI_USER="${PI_USER:-twox}"
PI_PW="${PI_PW:-}"
LAPTOP_IP="${LAPTOP_IP:-10.192.226.77}"
HERE="$(cd "$(dirname "$0")" && pwd)"

if [ -z "$PI_PW" ]; then
    echo "ERROR: PI_PW is not set. Ask the user for the password; do not guess."
    exit 1
fi

PSCP=(pscp -batch -pw "$PI_PW")
PLINK=(plink -ssh -batch -pw "$PI_PW" "${PI_USER}@${PI_HOST}")

echo "== 1. Copy sources to ${PI_USER}@${PI_HOST}:/home/twox/pi_core =="
if [ "${SKIP_DEPLOY:-0}" != "1" ]; then
    "${PLINK[@]}" "rm -rf /home/twox/pi_core_old && [ -d /home/twox/pi_core ] && mv /home/twox/pi_core /home/twox/pi_core_old || true"
    "${PSCP[@]}" -r "$HERE" "${PI_USER}@${PI_HOST}:/home/twox/pi_core"
    # Remove any local build artifacts that got copied (they are Linux-irrelevant
    # but may be stale) — the Pi rebuilds in ./build.
    "${PLINK[@]}" "rm -rf /home/twox/pi_core/build /home/twox/pi_core/pi_core /home/twox/pi_core/pi_core_tests"
fi
echo "== sha256 spot-check =="
"${PLINK[@]}" "cd /home/twox/pi_core && sha256sum src/main.cpp CMakeLists.txt vendor/json.hpp | head -3"

echo "== 2. Toolchain check (g++, cmake) =="
if ! "${PLINK[@]}" "which g++ cmake >/dev/null 2>&1"; then
    echo "g++/cmake missing on the Pi. Run with ALLOW_APT=1 (needs sudo) or manually:"
    echo "  sudo apt install -y build-essential cmake libopencv-dev libzmq3-dev"
    if [ "${ALLOW_APT:-0}" = "1" ]; then
        echo "$PI_PW" | "${PLINK[@]}" "sudo -S apt install -y build-essential cmake libopencv-dev libzmq3-dev"
    else
        exit 1
    fi
fi

echo "== 3. Build (Release, -O3) =="
"${PLINK[@]}" "cd /home/twox/pi_core && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j 3"

echo "== 4. Unit tests (no camera) =="
"${PLINK[@]}" "cd /home/twox/pi_core && PI_CORE_TEST_TMP=/tmp ./build/pi_core_tests"

echo "== 5. HSV parity vs Pi's real cv2 (OpenCV 4.10) =="
"${PLINK[@]}" "cd /home/twox/pi_core && python3 tests/parity_hsv.py > /tmp/hsv_vectors.txt 2>/tmp/hsv_parity.log && ./build/pi_core_tests --vectors /tmp/hsv_vectors.txt && cat /tmp/hsv_parity.log"

echo "== 6. Camera probe (camera must be free: fuser -v /dev/video0) =="
"${PLINK[@]}" "cd /home/twox/pi_core && ./build/pi_core --probe"

echo "== 7. Full pipeline smoke, 12 s, no serial, 3 gates =="
"${PLINK[@]}" "cd /home/twox/pi_core && ./build/pi_core --duration 12 --no-serial --gates 3 --laptop-ip ${LAPTOP_IP}"

echo "== 8. Serial format smoke (--print-serial -> stdout) =="
echo "  # In ANOTHER Git Bash window, on the laptop, feed colors (PUB must bind on the laptop):"
echo "  #   python UniKostyl/out/send_colors.py 30 5567"
echo "  # (port 5567, because a running cv_kostyl.py usually holds 5557)"
"${PLINK[@]}" "cd /home/twox/pi_core && ./build/pi_core --duration 10 --gates 3 --print-serial --rgb-port 5567 --laptop-ip ${LAPTOP_IP} 2>/tmp/pi_core_run.log | tee /tmp/serial_lines.txt | grep -cE '^HB$' || true"
echo "  # ---- G lines (appear only while colors are fed): ----"
"${PLINK[@]}" "grep -E '^G[0-9]+' /tmp/serial_lines.txt | head -20 || echo '(no G lines — feed colors as above and rerun step 8)'"
echo "  # ---- log tail: ----"
"${PLINK[@]}" "tail -8 /tmp/pi_core_run.log"

echo "== 9. saved_gates.json restart test (after colors were sent once) =="
echo "  # a) feed colors during a run (step 8) -> file must appear:"
"${PLINK[@]}" "cat /home/twox/saved_gates.json 2>/dev/null || echo '(saved_gates.json not written yet — feed colors in step 8 first)'"
echo "  # b) restart pi_core WITHOUT the laptop -> gates track right away:"
echo "  #    plink ... './build/pi_core --duration 6 --gates 3 --no-serial'"
echo "  #    expect: 'Загружены цвета из /home/twox/saved_gates.json: G1, G2, G3'"

echo "DONE. Remember: kill any leftover rpi_elp_cam.py before step 6/7 (fuser -v /dev/video0) — the camera is exclusive."
