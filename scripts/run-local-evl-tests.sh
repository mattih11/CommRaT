#!/usr/bin/env bash
# run-local-evl-tests.sh
#
# Boots the RaTOS QEMU image locally, builds CommRaT inside the guest, and
# runs ctest. Mirrors the test-evl-runtime CI job for local iteration.
#
# Configuration is loaded from .commrat.env in the repository root. Any
# variable already set in the environment takes precedence over .commrat.env.
# For machine-specific overrides create .commrat.env.local (gitignored).
#
# Usage:
#   # Use pre-downloaded artifacts:
#   scripts/run-local-evl-tests.sh --wic path/to/ratos.wic --kernel path/to/vmlinuz
#
#   # Auto-download from the latest successful RaTOS main branch build:
#   scripts/run-local-evl-tests.sh
#
#   # Download from a specific workflow run:
#   RATOS_RUN_ID=12345678 scripts/run-local-evl-tests.sh
#
#   # Download from a specific release tag:
#   RATOS_RELEASE_TAG=v1.0.0 scripts/run-local-evl-tests.sh
#
# Prerequisites:
#   qemu-system-x86_64  (apt: qemu-system-x86)
#   kpartx              (apt: kpartx)
#   rsync
#   ssh / ssh-keygen
#   gh                  (needed for auto-download; must be authenticated)
#
# The script modifies a COPY of the wic image so the original is untouched.

set -euo pipefail

# ---------------------------------------------------------------------------
# Load .commrat.env as defaults (variables already exported take precedence)
# ---------------------------------------------------------------------------
_load_env() {
    local envfile="$1"
    [[ -f "$envfile" ]] || return 0
    local line key val
    while IFS= read -r line || [[ -n "$line" ]]; do
        # Skip comments and blank lines
        [[ "$line" =~ ^[[:space:]]*(#|$) ]] && continue
        key="${line%%=*}"
        val="${line#*=}"
        key="${key//[[:space:]]/}"
        [[ -z "$key" ]] && continue
        # Only assign if variable is not already set in the environment
        [[ -v "$key" ]] || printf -v "$key" '%s' "$val"
    done < "$envfile"
}

_EARLY_SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
_load_env "${_EARLY_SCRIPT_DIR}/../.commrat.env"
_load_env "${_EARLY_SCRIPT_DIR}/../.commrat.env.local"

# ---------------------------------------------------------------------------
# Defaults / argument parsing
# ---------------------------------------------------------------------------
WIC_PATH=""
KERNEL_PATH=""
RATOS_RELEASE_REPO="${RATOS_RELEASE_REPO:-}"
RATOS_RUN_ID="${RATOS_RUN_ID:-}"
RATOS_RELEASE_TAG="${RATOS_RELEASE_TAG:-}"
QEMU_MEMORY="${QEMU_MEMORY:-2G}"
QEMU_CPUS="${QEMU_CPUS:-2}"
SSH_PORT="${SSH_PORT:-2222}"
WORK_DIR="$(mktemp -d /tmp/commrat-evl-XXXXXX)"
CLEANUP_WORK_DIR=1

usage() {
    grep '^#' "$0" | sed 's/^# \?//' | tail -n +2 | head -n 20
    exit 1
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --wic)       WIC_PATH="$(realpath "$2")";    shift 2 ;;
        --kernel)    KERNEL_PATH="$(realpath "$2")"; shift 2 ;;
        --run-id)    RATOS_RUN_ID="$2";              shift 2 ;;
        --tag)       RATOS_RELEASE_TAG="$2";         shift 2 ;;
        --help|-h)   usage ;;
        *) echo "Unknown argument: $1" >&2; usage ;;
    esac
done

# ---------------------------------------------------------------------------
# Cleanup on exit
# ---------------------------------------------------------------------------
cleanup() {
    if [[ -f "$WORK_DIR/qemu.pid" ]]; then
        local pid
        pid="$(cat "$WORK_DIR/qemu.pid")"
        if kill -0 "$pid" 2>/dev/null; then
            echo "Stopping QEMU (pid $pid)..."
            kill "$pid" 2>/dev/null || true
            wait "$pid" 2>/dev/null || true
        fi
    fi
    # Detach any kpartx loop devices we may have left open
    if [[ -f "$WORK_DIR/loop_dev" ]]; then
        sudo kpartx -dv "$(cat "$WORK_DIR/wic_copy_path")" 2>/dev/null || true
    fi
    if [[ "$CLEANUP_WORK_DIR" -eq 1 ]]; then
        rm -rf "$WORK_DIR"
    fi
}
trap cleanup EXIT

# ---------------------------------------------------------------------------
# Locate the repository root (this script lives in scripts/)
# ---------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

cd "$REPO_ROOT"

# ---------------------------------------------------------------------------
# Download artifacts if not provided
# ---------------------------------------------------------------------------
if [[ -z "$WIC_PATH" || -z "$KERNEL_PATH" ]]; then
    if [[ -z "$RATOS_RELEASE_REPO" ]]; then
        echo "ERROR: RATOS_RELEASE_REPO is not set." >&2
        echo "       Set it in .commrat.env or export it before running this script." >&2
        echo "       Or pass --wic and --kernel to skip downloading." >&2
        exit 1
    fi

    DL_DIR="$WORK_DIR/artifacts"
    mkdir -p "$DL_DIR"

    # Optional auth token (for private repos; not needed for mattih11/RaTOS)
    if [[ -n "${RATOS_RELEASE_TOKEN:-}" ]]; then
        export GH_TOKEN="$RATOS_RELEASE_TOKEN"
    fi

    if [[ -n "$RATOS_RUN_ID" ]]; then
        echo "Downloading artifacts from workflow run ${RATOS_RUN_ID}..."
        gh run download "$RATOS_RUN_ID" \
            --repo "$RATOS_RELEASE_REPO" \
            --name ratos-evl-artifacts \
            --dir "$DL_DIR"
    elif [[ -n "$RATOS_RELEASE_TAG" ]]; then
        echo "Downloading artifacts from release tag ${RATOS_RELEASE_TAG}..."
        gh release download "$RATOS_RELEASE_TAG" \
            --repo "$RATOS_RELEASE_REPO" \
            --pattern "*container-amd64-vmlinuz" \
            --pattern "*container-amd64.wic.gz" \
            --dir "$DL_DIR"
    else
        # Default: latest successful run on the main branch
        echo "Locating latest successful RaTOS build on main..."
        RATOS_RUN_ID="$(gh run list \
            --repo "$RATOS_RELEASE_REPO" \
            --workflow build-and-publish.yml \
            --branch main \
            --status success \
            --limit 1 \
            --json databaseId \
            --jq '.[0].databaseId')"
        if [[ -z "$RATOS_RUN_ID" || "$RATOS_RUN_ID" == "null" ]]; then
            echo "ERROR: No successful RaTOS build found on main branch." >&2
            echo "       Pass --wic and --kernel, or --run-id <id>, or --tag <tag>." >&2
            exit 1
        fi
        echo "Downloading artifacts from run ${RATOS_RUN_ID}..."
        gh run download "$RATOS_RUN_ID" \
            --repo "$RATOS_RELEASE_REPO" \
            --name ratos-evl-artifacts \
            --dir "$DL_DIR"
    fi

    echo "Downloaded artifacts:"
    ls -lh "$DL_DIR/"

    # Decompress wic if compressed
    if compgen -G "$DL_DIR/*.wic.gz" > /dev/null; then
        WIC_GZ="$(ls "$DL_DIR"/*.wic.gz | head -1)"
        echo "Decompressing $WIC_GZ ..."
        gunzip "$WIC_GZ"
    fi
    WIC_PATH="$(ls "$DL_DIR"/*.wic | head -1)"
    KERNEL_PATH="$(ls "$DL_DIR"/*vmlinuz | head -1)"
fi

echo "Using wic image : $WIC_PATH"
echo "Using kernel    : $KERNEL_PATH"

# ---------------------------------------------------------------------------
# Copy wic image so we don't modify the user's original file
# ---------------------------------------------------------------------------
WIC_COPY="$WORK_DIR/ratos.wic"
echo "Copying wic image to $WIC_COPY ..."
cp "$WIC_PATH" "$WIC_COPY"
echo "$WIC_COPY" > "$WORK_DIR/wic_copy_path"

# ---------------------------------------------------------------------------
# Generate ephemeral SSH keypair
# ---------------------------------------------------------------------------
SSH_KEY="$WORK_DIR/ci_key"
ssh-keygen -t ed25519 -N "" -f "$SSH_KEY" -q

# ---------------------------------------------------------------------------
# Inject public key into the image
# ---------------------------------------------------------------------------
echo "Injecting SSH public key into guest image..."
sudo kpartx -av "$WIC_COPY"
echo "$WIC_COPY" > "$WORK_DIR/loop_dev"  # Signal cleanup to detach
sleep 1  # Wait for device nodes

LOOP_DEV="$(sudo kpartx -l "$WIC_COPY" | head -1 | awk '{print $1}')"
MOUNT_POINT="$WORK_DIR/mnt"
mkdir -p "$MOUNT_POINT"

sudo mount "/dev/mapper/$LOOP_DEV" "$MOUNT_POINT"
sudo mkdir -p "$MOUNT_POINT/root/.ssh"
sudo cp "${SSH_KEY}.pub" "$MOUNT_POINT/root/.ssh/authorized_keys"
sudo chmod 700 "$MOUNT_POINT/root/.ssh"
sudo chmod 600 "$MOUNT_POINT/root/.ssh/authorized_keys"
sudo umount "$MOUNT_POINT"
sudo kpartx -dv "$WIC_COPY"
rm -f "$WORK_DIR/loop_dev"  # Detached cleanly

# ---------------------------------------------------------------------------
# Boot QEMU
# ---------------------------------------------------------------------------
echo "Starting QEMU..."
qemu-system-x86_64 \
    -kernel "$KERNEL_PATH" \
    -append "root=/dev/vda1 rw console=ttyS0 quiet" \
    -drive "file=$WIC_COPY,format=raw,if=virtio" \
    -netdev "user,id=net0,hostfwd=tcp:127.0.0.1:${SSH_PORT}-:22" \
    -device virtio-net-pci,netdev=net0 \
    -m "$QEMU_MEMORY" -smp "$QEMU_CPUS" \
    -nographic \
    -serial mon:stdio \
    2>&1 | tee "$WORK_DIR/qemu.log" &
echo $! > "$WORK_DIR/qemu.pid"

# ---------------------------------------------------------------------------
# Wait for SSH to become available
# ---------------------------------------------------------------------------
echo "Waiting for guest SSH (up to 3 minutes)..."
SSH_OPTS="-i ${SSH_KEY} -o StrictHostKeyChecking=no -o BatchMode=yes -p ${SSH_PORT}"
for i in $(seq 1 90); do
    if ssh ${SSH_OPTS} root@127.0.0.1 true 2>/dev/null; then
        echo "SSH ready after $((i * 2))s"
        break
    fi
    if [[ "$i" -eq 90 ]]; then
        echo "ERROR: Timeout waiting for SSH after 180s" >&2
        echo "Last 20 lines of QEMU log:" >&2
        tail -20 "$WORK_DIR/qemu.log" >&2
        exit 1
    fi
    sleep 2
done

# ---------------------------------------------------------------------------
# Run tests via run-evl-tests.sh (shared with CI; SSH_KEY and SSH_PORT are
# passed as environment variables so no hardcoded paths or symlinks needed)
# ---------------------------------------------------------------------------
SSH_KEY="$SSH_KEY" SSH_PORT="$SSH_PORT" bash scripts/ci/run-evl-tests.sh

# ---------------------------------------------------------------------------
# Done
# ---------------------------------------------------------------------------
echo ""
echo "All tests passed. QEMU is still running."
echo "  SSH in: ssh -i $SSH_KEY -p $SSH_PORT root@127.0.0.1"
echo "  Stop  : kill \$(cat $WORK_DIR/qemu.pid)"
echo ""
echo "Press Ctrl-C to stop QEMU and clean up."
wait "$(cat "$WORK_DIR/qemu.pid")" 2>/dev/null || true
