#!/usr/bin/env bash
# Prepare a temporary native camera service build on the Pi.
# This script performs SSH/rsync only when a human runs it explicitly.
# It never starts a service and never enables a systemd unit.
set -euo pipefail

PI_HOST="${PI_HOST:-10.132.122.51}"
PI_USER="${PI_USER:-twox}"
PI_DIR="${PI_DIR:-/tmp/camera_cpp_smoke}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOCAL_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
STAMP="$(date +%Y%m%d_%H%M%S)"
BACKUP_DIR="${PI_DIR}/backup/${STAMP}"
REMOTE="${PI_USER}@${PI_HOST}"

for tool in ssh rsync; do
    if ! command -v "${tool}" >/dev/null 2>&1; then
        echo "missing local tool: ${tool}" >&2
        exit 2
    fi
done

echo "Preparing temporary Pi build at ${REMOTE}:${PI_DIR} (no service start)."
ssh "${REMOTE}" bash -s -- "${PI_DIR}" "${BACKUP_DIR}" <<'REMOTE_SCRIPT'
set -euo pipefail
PI_DIR="$1"
BACKUP_DIR="$2"
mkdir -p "${BACKUP_DIR}"
for tool in cmake g++; do
    if ! command -v "${tool}" >/dev/null 2>&1; then
        echo "missing Pi tool: ${tool}" >&2
        exit 3
    fi
done
for name in saved_gates.json camera_settings.json field_map.json distance_calib.json; do
    if [ -f "${PI_DIR}/${name}" ]; then
        cp -a "${PI_DIR}/${name}" "${BACKUP_DIR}/${name}"
    fi
done
(
    cd "${PI_DIR}"
    sha256sum saved_gates.json camera_settings.json field_map.json distance_calib.json 2>/dev/null \
        > "${BACKUP_DIR}/sha256_before.txt" || true
)
echo "Backup: ${BACKUP_DIR}"
REMOTE_SCRIPT

rsync -az \
    --exclude '.git/' --exclude 'build*/' --exclude 'local/' \
    --exclude 'graphify-out/' --exclude '.superpowers/' \
    --exclude '*.pem' --exclude '*.key' --exclude 'id_ed25519*' \
    --exclude 'id_rsa*' --exclude 'id_ecdsa*' --exclude '.env*' \
    --exclude 'known_hosts' --exclude 'secrets*' \
    "${LOCAL_ROOT}/" "${REMOTE}:${PI_DIR}/"

ssh "${REMOTE}" bash -s -- "${PI_DIR}" <<'REMOTE_SCRIPT'
set -euo pipefail
PI_DIR="$1"
cd "${PI_DIR}"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
echo "Build complete: ${PI_DIR}/build/src/camera_service"
echo "Start manually with --no-serial; see docs/DEPLOY.md."
REMOTE_SCRIPT

echo "Deploy preparation complete. Service was NOT started."
