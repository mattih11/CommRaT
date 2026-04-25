#!/usr/bin/env bash
# run-in-container.sh
#
# Run a command (or interactive shell) inside the RaTOS dev container with the
# CommRaT source tree mounted. Optionally build with a CMake preset first.
#
# Configuration is loaded from .commrat.env. Override individual values by
# creating .commrat.env.local (gitignored) or exporting them beforehand.
#
# Usage:
#   # Interactive shell (no build):
#   scripts/run-in-container.sh
#
#   # Build with default (STD) preset, then open shell:
#   scripts/run-in-container.sh --build
#
#   # Build with EVL preset, then open shell:
#   scripts/run-in-container.sh --build evl
#
#   # Build only (no shell):
#   scripts/run-in-container.sh --build evl --no-shell
#
#   # Run a specific binary after building with default preset:
#   scripts/run-in-container.sh --build -- ./build/default/examples/example_commands
#
#   # Run an arbitrary command without building:
#   scripts/run-in-container.sh -- bash -c "ls build/evl/examples/"
#
# Build outputs land in build/<preset>/ on the host (bind-mounted), so they
# persist between container invocations. EVL binaries require an EVL kernel to
# run; use scripts/run-local-evl-tests.sh for QEMU-based EVL runtime testing.

set -euo pipefail

# ---------------------------------------------------------------------------
# Load .commrat.env as defaults (exported variables take precedence)
# ---------------------------------------------------------------------------
_load_env() {
    local envfile="$1"
    [[ -f "$envfile" ]] || return 0
    local line key val
    while IFS= read -r line || [[ -n "$line" ]]; do
        [[ "$line" =~ ^[[:space:]]*(#|$) ]] && continue
        key="${line%%=*}"
        val="${line#*=}"
        key="${key//[[:space:]]/}"
        [[ -z "$key" ]] && continue
        [[ -v "$key" ]] || printf -v "$key" '%s' "$val"
    done < "$envfile"
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
_load_env "$REPO_ROOT/.commrat.env"
_load_env "$REPO_ROOT/.commrat.env.local"

# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------
BUILD_PRESET=""
OPEN_SHELL=1
USER_CMD=()

usage() {
    grep '^#' "$0" | sed 's/^# \?//' | tail -n +2 | head -n 30
    exit 1
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build)
            # Optional preset name after --build (default: "default")
            if [[ $# -gt 1 && "$2" != --* && "$2" != -- ]]; then
                BUILD_PRESET="$2"; shift 2
            else
                BUILD_PRESET="default"; shift 1
            fi
            ;;
        --no-shell)
            OPEN_SHELL=0; shift ;;
        --)
            shift; USER_CMD=("$@"); OPEN_SHELL=0; break ;;
        --help|-h)
            usage ;;
        *)
            echo "Unknown argument: $1" >&2; usage ;;
    esac
done

# ---------------------------------------------------------------------------
# Detect container runtime
# ---------------------------------------------------------------------------
RUNTIME=""
for rt in docker podman; do
    if command -v "$rt" &>/dev/null; then
        RUNTIME="$rt"
        break
    fi
done
if [[ -z "$RUNTIME" ]]; then
    echo "ERROR: Neither docker nor podman found in PATH." >&2
    exit 1
fi

IMAGE="${RATOS_IMAGE_REF:-ghcr.io/mattih11/ratos-dev-image:latest}"

# ---------------------------------------------------------------------------
# Compose the in-container command
# ---------------------------------------------------------------------------
IN_CONTAINER_CMD=()

if [[ -n "$BUILD_PRESET" ]]; then
    IN_CONTAINER_CMD+=(
        "set -euo pipefail"
        "echo '--- Building with preset: ${BUILD_PRESET} ---'"
        "cmake --preset ${BUILD_PRESET}"
        "cmake --build --preset ${BUILD_PRESET} --parallel \$(nproc)"
        "echo '--- Build complete ---'"
    )
fi

if [[ ${#USER_CMD[@]} -gt 0 ]]; then
    # Wrap user command in quotes for passing through bash -c
    IN_CONTAINER_CMD+=("$(printf '%q ' "${USER_CMD[@]}")")
elif [[ "$OPEN_SHELL" -eq 1 ]]; then
    IN_CONTAINER_CMD+=("exec bash")
fi

# ---------------------------------------------------------------------------
# Run
# ---------------------------------------------------------------------------
DOCKER_FLAGS=(
    --rm
    --volume "${REPO_ROOT}:/workspace"
    --workdir /workspace
)

# Pass through tims_router_tcp host networking so STD builds can reach the
# router running on the host. (Only meaningful for STD platform tests.)
if [[ "$RUNTIME" == "docker" ]]; then
    DOCKER_FLAGS+=(--network host)
else
    # podman uses --network=host syntax
    DOCKER_FLAGS+=(--network=host)
fi

# Interactive + TTY when opening a shell or running a terminal command
if [[ "$OPEN_SHELL" -eq 1 || ${#USER_CMD[@]} -gt 0 ]]; then
    DOCKER_FLAGS+=(-it)
fi

if [[ ${#IN_CONTAINER_CMD[@]} -gt 0 ]]; then
    JOINED="$(IFS='; '; echo "${IN_CONTAINER_CMD[*]}")"
    "$RUNTIME" run "${DOCKER_FLAGS[@]}" "$IMAGE" bash -c "$JOINED"
else
    # No build, no user command: just open an interactive shell
    "$RUNTIME" run "${DOCKER_FLAGS[@]}" -it "$IMAGE" bash
fi
