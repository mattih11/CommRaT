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

SSH_OPTS="-i ci_key -o StrictHostKeyChecking=no -o BatchMode=yes -p 2222"
REMOTE="root@127.0.0.1"
GUEST_DIR="/root/CommRaT"
BUILD_TYPE="${BUILD_TYPE:-Release}"

ssh_exec() {
    ssh $SSH_OPTS "$REMOTE" "$@"
}

# ---------------------------------------------------------------------------
# Transfer source
# ---------------------------------------------------------------------------
echo "Transferring CommRaT source to guest..."
rsync -az --delete \
    --exclude=build \
    --exclude=.git \
    -e "ssh $SSH_OPTS" \
    ./ "$REMOTE:$GUEST_DIR/"

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------
echo "Configuring CommRaT inside guest..."
ssh_exec bash -c "
    set -euo pipefail
    cd $GUEST_DIR
    cmake -B build \
        -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
        -DCOMMRAT_BUILD_EXAMPLES=OFF \
        2>&1
"

echo "Building CommRaT inside guest..."
ssh_exec bash -c "
    set -euo pipefail
    cd $GUEST_DIR
    cmake --build build --parallel \$(nproc) 2>&1
"

# ---------------------------------------------------------------------------
# Test
# ---------------------------------------------------------------------------
echo "Running ctest inside guest..."
ssh_exec bash -c "
    set -euo pipefail
    cd $GUEST_DIR
    ctest --test-dir build --output-on-failure 2>&1
"

echo "EVL runtime tests completed successfully."
