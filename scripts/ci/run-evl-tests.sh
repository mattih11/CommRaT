#!/usr/bin/env bash
# run-evl-tests.sh
#
# Transfers CommRaT source into a QEMU guest (already booted and SSH-ready),
# builds it, and runs ctest. Called from the test-evl-runtime CI job.
#
# Prerequisites (set up by the CI job before calling this script):
#   - ci_key          : SSH private key (in the working directory)
#   - QEMU guest      : listening on 127.0.0.1:2222, SSH authorized via ci_key

set -euo pipefail

# SSH parameters — configurable so both the local QEMU script and CI can use
# this script with their respective key paths and host ports.
SSH_PORT="${SSH_PORT:-2222}"
SSH_KEY="${SSH_KEY:-ci_key}"
SSH_OPTS="-i ${SSH_KEY} -o StrictHostKeyChecking=no -o BatchMode=yes -p ${SSH_PORT}"
REMOTE="root@127.0.0.1"
GUEST_DIR="/root/CommRaT"

ssh_exec() {
    ssh ${SSH_OPTS} "$REMOTE" "$@"
}

# ---------------------------------------------------------------------------
# Transfer source
# ---------------------------------------------------------------------------
echo "Transferring CommRaT source to guest..."
rsync -az --delete \
    --exclude=build \
    --exclude=.git \
    -e "ssh ${SSH_OPTS}" \
    ./ "$REMOTE:$GUEST_DIR/"

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------
echo "Configuring CommRaT inside guest..."
ssh_exec bash -c "
    set -euo pipefail
    cd $GUEST_DIR
    cmake --preset default 2>&1
"

echo "Building CommRaT inside guest..."
ssh_exec bash -c "
    set -euo pipefail
    cd $GUEST_DIR
    cmake --build --preset default --parallel \$(nproc) 2>&1
"

# ---------------------------------------------------------------------------
# Test
# ---------------------------------------------------------------------------
echo "Running ctest inside guest..."
ssh_exec bash -c "
    set -euo pipefail
    cd $GUEST_DIR
    ctest --preset default 2>&1
"

echo "EVL runtime tests completed successfully."
