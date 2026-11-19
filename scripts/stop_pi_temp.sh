#!/usr/bin/env bash
# Stop only the temporary camera_service started from the recorded PID file.
# No broad pkill: an unrelated service must never be terminated.
set -euo pipefail

PI_HOST="${PI_HOST:-10.132.122.51}"
PI_USER="${PI_USER:-twox}"
PI_DIR="${PI_DIR:-/tmp/camera_cpp_smoke}"
REMOTE="${PI_USER}@${PI_HOST}"
PORT="${PORT:-5555}"

ssh "${REMOTE}" bash -s -- "${PI_DIR}" "${PORT}" <<'REMOTE_SCRIPT'
set -euo pipefail
PI_DIR="$1"
PORT="$2"
PID_FILE="${PI_DIR}/service.pid"
if [ ! -f "${PID_FILE}" ]; then
    echo "no PID file at ${PID_FILE}; nothing to stop" >&2
    exit 4
fi
PID="$(tr -dc '0-9' < "${PID_FILE}")"
if [ -z "${PID}" ]; then
    echo "PID file is empty; nothing to stop" >&2
    exit 4
fi
if [ ! -r "/proc/${PID}/cmdline" ]; then
    echo "process ${PID} is not running; removing stale PID file" >&2
    rm -f "${PID_FILE}"
    exit 0
fi
CMD="$(tr '\0' ' ' < "/proc/${PID}/cmdline")"
CWD="$(readlink -f "/proc/${PID}/cwd" || true)"
case "${CMD}" in
    *camera_service*) ;;
    *)
        echo "refusing to kill PID ${PID}: cmdline is not camera_service" >&2
        echo "observed: ${CMD}" >&2
        exit 5
        ;;
esac
if [ "${CWD}" != "$(readlink -f "${PI_DIR}")" ]; then
    echo "refusing to kill PID ${PID}: working directory is ${CWD}, expected ${PI_DIR}" >&2
    exit 5
fi
kill -TERM "${PID}"
for _ in 1 2 3 4 5 6 7 8 9 10; do
    if [ ! -d "/proc/${PID}" ]; then
        break
    fi
    sleep 0.3
done
if [ -d "/proc/${PID}" ]; then
    echo "process ${PID} did not exit after TERM; kill -KILL was NOT sent" >&2
    exit 6
fi
rm -f "${PID_FILE}"
if ss -ltn | grep -q ":${PORT} "; then
    echo "warning: port ${PORT} is still listening after process exit" >&2
else
    echo "stopped PID ${PID}; port ${PORT} closed"
fi
REMOTE_SCRIPT

echo "Stop procedure finished. Verify configs were restored; see docs/DEPLOY.md."
