#!/usr/bin/env bash
# evl-dev.sh
#
# Boot the RaTOS EVL kernel in QEMU and develop against it locally.
#
# Two complementary build paths:
#
#   --build [preset]   Build inside the QEMU guest (default preset: default).
#                      Requires nothing on the host beyond ssh/rsync/qemu.
#                      Source is rsynced into the guest and built there.
#
#   --cross [preset]   Cross-compile on the HOST using the RaTOS SDK
#                      (default preset: evl-cross), then rsync binaries to
#                      the guest. Much faster for iterative development.
#                      Requires the SDK to be installed (see --sdk-dir).
#
# Action flags execute in order: cross -> build -> test -> run -> shell.
# With no action flags the default is --cross evl-cross --test (mirrors CI).
#
# Usage:
#   # Default - build inside QEMU + run tests (mirrors CI):
#   scripts/evl-dev.sh
#
#   # Interactive shell only (nothing built):
#   scripts/evl-dev.sh --shell
#
#   # Cross-compile EVL on host, deploy binaries, open shell:
#   scripts/evl-dev.sh --cross --shell
#
#   # Cross-compile, deploy, run tests:
#   scripts/evl-dev.sh --cross --test
#
#   # Cross-compile specific preset, then run a binary:
#   scripts/evl-dev.sh --cross evl-cross --run build/evl-cross/test/test_io_spec
#
#   # Build inside guest with EVL preset, then run tests:
#   scripts/evl-dev.sh --build evl --test
#
#   # Run multiple binaries already deployed:
#   scripts/evl-dev.sh --run /root/commrat/test/test_io_spec \
#                      --run /root/commrat/test/test_registry_utils
#
#   # Point at a local ISAR image directory (multiple images in the same folder):
#   scripts/evl-dev.sh --images-dir /path/to/isar/deploy/images/container-amd64 \
#                      --image-name ratos-evl-image
#   # Or put these in .commrat.env.local to avoid passing them every time:
#   #   LOCAL_IMAGES_DIR=/path/to/ratos/build/tmp/deploy/images/container-amd64
#   #   LOCAL_IMAGE_NAME=ratos-evl-image
#
#   # Use pre-downloaded artifacts:
#   scripts/evl-dev.sh [flags] --ext4 path/to/ratos.ext4 \
#                               --kernel path/to/vmlinuz \
#                               --initrd path/to/initrd.img
#
#   # Skip all downloads with a local SDK and local image (no gh, no network):
#   # Put these in .commrat.env.local once, then just run scripts/evl-dev.sh:
#   #   EVL_SDK_DIR=/path/to/ratos-sdk
#   #   LOCAL_EXT4=/path/to/ratos.ext4
#   #   LOCAL_KERNEL=/path/to/vmlinuz
#   #   LOCAL_INITRD=/path/to/initrd.img
#   scripts/evl-dev.sh --cross --test
#
#   # Download from a specific run or release tag:
#   RATOS_RUN_ID=12345678 scripts/evl-dev.sh [flags]
#   RATOS_RELEASE_TAG=v1.0.0 scripts/evl-dev.sh [flags]
#
# SDK setup (for --cross):
#   The SDK is auto-downloaded and extracted on first use.
#   Default cache: .evl-cache/sdk  (gitignored; override via EVL_SDK_DIR in .commrat.env.local)
#   Requires 'gh' to be authenticated for auto-download.
#
# Artifact caching:
#   ext4 + vmlinuz + initrd are cached in .evl-cache/ (gitignored) and
#   reused when the resolved run ID or release tag has not changed.
#   Delete .evl-cache/ to force a fresh download.
#
# Prerequisites:
#   qemu-system-x86_64  (apt: qemu-system-x86)
#   rsync, ssh, ssh-keygen
#   gh                  (for auto-download; must be authenticated)
#
# The script modifies a COPY of the ext4 image; the cache is never mutated.

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

_EARLY_SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Load .commrat.env.local FIRST so user overrides take precedence,
# then .commrat.env fills in any remaining unset defaults.
_load_env "${_EARLY_SCRIPT_DIR}/../.commrat.env.local"
_load_env "${_EARLY_SCRIPT_DIR}/../.commrat.env"

# ---------------------------------------------------------------------------
# Defaults
# ---------------------------------------------------------------------------
EXT4_PATH=""
KERNEL_PATH=""
INITRD_PATH=""
RATOS_RELEASE_REPO="${RATOS_RELEASE_REPO:-}"
RATOS_RUN_ID="${RATOS_RUN_ID:-}"
RATOS_RELEASE_TAG="${RATOS_RELEASE_TAG:-}"
QEMU_MEMORY="${QEMU_MEMORY:-4G}"
QEMU_CPUS="${QEMU_CPUS:-4}"
SSH_PORT="${SSH_PORT:-22222}"
EVL_SDK_DIR="${EVL_SDK_DIR:-.evl-cache/sdk}"
# LOCAL_EXT4 / LOCAL_KERNEL / LOCAL_INITRD may be set in .commrat.env.local
# to skip online artifact download entirely (useful for offline/iterative work).
LOCAL_EXT4="${LOCAL_EXT4:-}"
LOCAL_KERNEL="${LOCAL_KERNEL:-}"
LOCAL_INITRD="${LOCAL_INITRD:-}"
# LOCAL_IMAGES_DIR + LOCAL_IMAGE_NAME: point at an ISAR deploy directory.
# The script globs for <image>-*-container-*.ext4 / -vmlinuz / -initrd.img.
# LOCAL_IMAGE_NAME selects which image when multiple images share the folder.
LOCAL_IMAGES_DIR="${LOCAL_IMAGES_DIR:-}"
LOCAL_IMAGE_NAME="${LOCAL_IMAGE_NAME:-}"

DO_CROSS=""          # preset for host cross-compile (empty = skip)
DO_BUILD=""          # preset for in-guest build (empty = skip)
DO_TEST=0
RUN_CMDS=()
DO_SHELL=0
DO_CONSOLE=0         # attach serial console to stdio (no SSH)
DO_GEN_DESCRIPTORS=0 # run --commrat-inspect on all modules in QEMU, sync back

WORK_DIR="$(mktemp -d /tmp/commrat-evl-XXXXXX)"
CLEANUP_WORK_DIR=1

# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------
usage() {
    grep '^#' "$0" | sed 's/^# \?//' | tail -n +2 | head -n 40
    exit 1
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --ext4)    EXT4_PATH="$(realpath "$2")";    shift 2 ;;
        --kernel)  KERNEL_PATH="$(realpath "$2")";  shift 2 ;;
        --initrd)  INITRD_PATH="$(realpath "$2")";  shift 2 ;;
        --run-id)  RATOS_RUN_ID="$2";               shift 2 ;;
        --tag)     RATOS_RELEASE_TAG="$2";          shift 2 ;;
        --sdk-dir) EVL_SDK_DIR="$2";                shift 2 ;;
        --images-dir)
            LOCAL_IMAGES_DIR="$(realpath "$2")"; shift 2 ;;
        --image-name)
            LOCAL_IMAGE_NAME="$2";               shift 2 ;;
        --cross)
            if [[ $# -gt 1 && "$2" != --* ]]; then
                DO_CROSS="$2"; shift 2
            else
                DO_CROSS="evl-cross"; shift 1
            fi
            ;;
        --build)
            if [[ $# -gt 1 && "$2" != --* ]]; then
                DO_BUILD="$2"; shift 2
            else
                DO_BUILD="default"; shift 1
            fi
            ;;
        --test)    DO_TEST=1;          shift   ;;
        --run)     RUN_CMDS+=("$2");   shift 2 ;;
        --generate-descriptors)
            DO_GEN_DESCRIPTORS=1
            if [[ -n "${2:-}" && "${2:0:1}" != "-" ]]; then
                DO_CROSS="$2"; shift 2
            else
                DO_CROSS="evl-cross"; shift 1
            fi
            ;;
        --shell)   DO_SHELL=1;         shift   ;;
        --console) DO_CONSOLE=1;       shift   ;;
        --help|-h) usage ;;
        *) echo "Unknown argument: $1" >&2; usage ;;
    esac
done

# Default when no action flags given: cross-compile + test (mirrors CI)
if [[ -z "$DO_CROSS" && -z "$DO_BUILD" && "$DO_TEST" -eq 0 \
      && ${#RUN_CMDS[@]} -eq 0 && "$DO_SHELL" -eq 0 && "$DO_CONSOLE" -eq 0 ]]; then
    DO_CROSS="evl-cross"
    DO_TEST=1
fi

# ---------------------------------------------------------------------------
# Cleanup on exit
# ---------------------------------------------------------------------------
cleanup() {
    if [[ -f "$WORK_DIR/qemu.pid" ]]; then
        local pid
        pid="$(cat "$WORK_DIR/qemu.pid")"
        if kill -0 "$pid" 2>/dev/null; then
            echo "Stopping QEMU (pid $pid)..."
            kill -9 "$pid" 2>/dev/null || true
            wait "$pid" 2>/dev/null || true
        fi
    fi
    if [[ -f "$WORK_DIR/loop_mounted" ]]; then
        sudo umount "$WORK_DIR/mnt" 2>/dev/null || true
        rm -f "$WORK_DIR/loop_mounted"
    fi
    if [[ "$CLEANUP_WORK_DIR" -eq 1 ]]; then
        rm -rf "$WORK_DIR"
    fi
}
trap cleanup EXIT

# ---------------------------------------------------------------------------
# Locate repository root (this script lives in scripts/)
# ---------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

cd "$REPO_ROOT"

# Resolve relative EVL_SDK_DIR against REPO_ROOT
[[ "$EVL_SDK_DIR" != /* ]] && EVL_SDK_DIR="$REPO_ROOT/$EVL_SDK_DIR"

# ---------------------------------------------------------------------------
# Persistent cache directory and GitHub auth (used by SDK + artifact download)
# ---------------------------------------------------------------------------
CACHE_DIR="$REPO_ROOT/.evl-cache"

if [[ -n "${RATOS_RELEASE_TOKEN:-}" ]]; then
    export GH_TOKEN="$RATOS_RELEASE_TOKEN"
fi

# ---------------------------------------------------------------------------
# Helper: resolve ext4/kernel/initrd from an ISAR images directory.
# ISAR naming convention:
#   <image>-<machine>-container-<arch>.ext4
#   <image>-<machine>-container-<arch>-vmlinuz
#   <image>-<machine>-container-<arch>-initrd.img
#
# Errors if the image name is ambiguous (zero or multiple matches).
# Sets EXT4_PATH, KERNEL_PATH, INITRD_PATH in the caller's scope.
# ---------------------------------------------------------------------------
_resolve_isar_images() {
    local dir="$1" name="$2"

    # Glob each artifact type — only exact ISAR suffixes to avoid .wic.gz etc.
    local ext4_files kernel_files initrd_files
    mapfile -t ext4_files   < <(ls "$dir/${name}"*container*.ext4    2>/dev/null | grep -v '\.wic\.' || true)
    mapfile -t kernel_files < <(ls "$dir/${name}"*container*-vmlinuz  2>/dev/null || true)
    mapfile -t initrd_files < <(ls "$dir/${name}"*container*-initrd.img 2>/dev/null || true)

    # Validate: exactly one match required for each
    local ok=1
    if [[ ${#ext4_files[@]} -eq 0 ]]; then
        echo "ERROR: No .ext4 found for image '${name}' in ${dir}" >&2
        echo "       Available images:" >&2
        ls "$dir"/*.ext4 2>/dev/null | grep -v '\.wic\.' | \
            sed 's|-[^-]*-container.*||;s|.*/||' | sort -u | sed 's/^/         /' >&2
        ok=0
    elif [[ ${#ext4_files[@]} -gt 1 ]]; then
        echo "ERROR: Multiple .ext4 files match '${name}' in ${dir}:" >&2
        printf '         %s\n' "${ext4_files[@]}" >&2
        echo "       Set LOCAL_IMAGE_NAME more specifically." >&2
        ok=0
    fi
    if [[ ${#kernel_files[@]} -eq 0 ]]; then
        echo "ERROR: No vmlinuz found for image '${name}' in ${dir}" >&2; ok=0
    elif [[ ${#kernel_files[@]} -gt 1 ]]; then
        echo "ERROR: Multiple vmlinuz files match '${name}' in ${dir}" >&2; ok=0
    fi
    if [[ ${#initrd_files[@]} -eq 0 ]]; then
        echo "ERROR: No initrd.img found for image '${name}' in ${dir}" >&2; ok=0
    elif [[ ${#initrd_files[@]} -gt 1 ]]; then
        echo "ERROR: Multiple initrd.img files match '${name}' in ${dir}" >&2; ok=0
    fi
    [[ "$ok" -eq 1 ]] || return 1

    EXT4_PATH="${ext4_files[0]}"
    KERNEL_PATH="${kernel_files[0]}"
    INITRD_PATH="${initrd_files[0]}"
    echo "Resolved ISAR image '${name}' from ${dir}:"
    echo "  ext4  : $EXT4_PATH"
    echo "  kernel: $KERNEL_PATH"
    echo "  initrd: $INITRD_PATH"
}


# Supports .gz, .xz, .zst.  Returns the decompressed path via stdout.
# For plain files (no recognised extension) the original path is echoed back.
# ---------------------------------------------------------------------------
_decompress_artifact() {
    local src="$1" dst_name="$2"   # dst_name = basename of the output file
    local dst="$WORK_DIR/${dst_name}"
    case "$src" in
        *.gz)
            echo "Decompressing ${src} -> ${dst} ..." >&2
            gunzip -c "$src" > "$dst"
            echo "$dst" ;;
        *.xz)
            echo "Decompressing ${src} -> ${dst} ..." >&2
            xz -d -c "$src" > "$dst"
            echo "$dst" ;;
        *.zst)
            echo "Decompressing ${src} -> ${dst} ..." >&2
            zstd -d -c "$src" > "$dst"
            echo "$dst" ;;
        *)
            echo "$src" ;;
    esac
}

# ---------------------------------------------------------------------------
# Helper: extract a SDK archive to a target directory.
# Detects .tar.xz, .tar.gz, .tar.zst, plain .xz (single-file tarball).
# ---------------------------------------------------------------------------
_extract_sdk_archive() {
    local archive="$1" dest="$2"
    echo "Extracting SDK archive ${archive} -> ${dest} ..." >&2
    rm -rf "$dest"
    mkdir -p "$dest"
    case "$archive" in
        *.tar.xz|*.tar.gz|*.tar.zst|*.tgz)
            tar -xaf "$archive" -C "$dest" --strip-components=1 2>/dev/null || true ;;
        *.xz)
            # Single-file .xz (some RaTOS SDK releases are just a compressed tarball)
            xz -d -c "$archive" | tar -x -C "$dest" --strip-components=1 2>/dev/null || true ;;
        *.gz)
            gunzip -c "$archive" | tar -x -C "$dest" --strip-components=1 2>/dev/null || true ;;
        *)
            echo "ERROR: unrecognised SDK archive format: ${archive}" >&2
            return 1 ;;
    esac
    if [[ ! -f "$dest/usr/include/evl/evl.h" ]]; then
        echo "ERROR: SDK extraction failed — evl/evl.h not found in ${dest}" >&2
        return 1
    fi
    echo "SDK extracted to ${dest}" >&2
}

# ---------------------------------------------------------------------------
# Helper: ensure the SDK's gcc-sysroot-wrapper.sh points at the current
# EVL_SDK_DIR. The wrapper ships with GCC_SYSROOT= empty (or pointing at the
# ISAR build host). Call this after any SDK is located — whether freshly
# downloaded, archive-extracted, or a pre-existing local directory.
# ---------------------------------------------------------------------------
_relocate_sdk() {
    local sdk="$1"
    local wrapper="${sdk}/usr/bin/gcc-sysroot-wrapper.sh"
    [[ -f "$wrapper" ]] || return 0   # no wrapper — nothing to do
    [[ -w "$wrapper" ]] || return 0   # not writable (e.g. root-owned sdkchroot) — skip;
                                      # isar-sdk-toolchain.cmake uses .bin compilers directly

    local current
    current="$(grep '^GCC_SYSROOT=' "$wrapper" | head -1 | cut -d= -f2- | tr -d '"' || true)"
    if [[ "$current" == "$sdk" ]]; then
        return 0   # already relocated to this path
    fi

    echo "Relocating SDK gcc wrapper: GCC_SYSROOT=${sdk}"
    if command -v patchelf &>/dev/null && [[ -x "${sdk}/relocate-sdk.sh" ]]; then
        "${sdk}/relocate-sdk.sh" 2>/dev/null || true
    fi
    # Always patch the wrapper directly — relocate-sdk.sh may not update it.
    sed -i "s|^GCC_SYSROOT=.*|GCC_SYSROOT=\"${sdk}\"|" "$wrapper"
    echo "SDK wrapper relocated."
}

# ---------------------------------------------------------------------------
# Cross-compile on host (--cross path, before QEMU boot)
# ---------------------------------------------------------------------------
if [[ -n "$DO_CROSS" ]]; then
    # Ensure the ISAR SDK is extracted to EVL_SDK_DIR.
    # The SDK is a plain tarball (amd64 Debian sysroot with libevl and all
    # CommRaT dependencies). We use the host compiler and add the SDK to
    # CMake search paths via cmake/isar-sdk-toolchain.cmake — no relocation,
    # no environment-setup sourcing needed.
    #
    # If EVL_SDK_DIR already contains a valid SDK (set via .commrat.env.local
    # or --sdk-dir), the download/extraction step is skipped entirely.
    SDK_KEY_FILE="${CACHE_DIR}/.sdk_cache_key"
    _SDK_KEY=""
    [[ -n "$RATOS_RELEASE_TAG" ]] && _SDK_KEY="tag:${RATOS_RELEASE_TAG}"

    _SDK_STALE=0
    # If EVL_SDK_DIR is an archive file, auto-extract it to a sibling directory.
    if [[ -f "$EVL_SDK_DIR" ]]; then
        _SDK_ARCHIVE="$EVL_SDK_DIR"
        # Derive extraction dir: strip all compression/tar extensions, append -extracted
        _SDK_EXTRACT_DIR="${_SDK_ARCHIVE%.tar.*}"
        _SDK_EXTRACT_DIR="${_SDK_EXTRACT_DIR%.xz}"
        _SDK_EXTRACT_DIR="${_SDK_EXTRACT_DIR%.gz}"
        _SDK_EXTRACT_DIR="${_SDK_EXTRACT_DIR%.zst}"
        _SDK_EXTRACT_DIR="${_SDK_EXTRACT_DIR}-extracted"
        if [[ -f "${_SDK_EXTRACT_DIR}/usr/include/evl/evl.h" ]]; then
            echo "Using already-extracted SDK at ${_SDK_EXTRACT_DIR}"
        else
            echo "EVL_SDK_DIR points to archive — extracting to ${_SDK_EXTRACT_DIR} ..."
            _extract_sdk_archive "$_SDK_ARCHIVE" "$_SDK_EXTRACT_DIR"
        fi
        EVL_SDK_DIR="$_SDK_EXTRACT_DIR"
        export EVL_SDK_DIR
    fi

    if [[ -f "$EVL_SDK_DIR/usr/include/evl/evl.h" ]]; then
        echo "Using local RaTOS SDK at ${EVL_SDK_DIR} (skipping download)"
        _SDK_STALE=0
        _relocate_sdk "$EVL_SDK_DIR"
    elif [[ ! -d "$EVL_SDK_DIR/usr" ]]; then
        _SDK_STALE=1
        echo "RaTOS SDK not found at ${EVL_SDK_DIR} — will download and extract..."
    elif [[ -n "$_SDK_KEY" && \
          ( ! -f "$SDK_KEY_FILE" || \
            "$(cat "$SDK_KEY_FILE" 2>/dev/null)" != "$_SDK_KEY" ) ]]; then
        _SDK_STALE=1
        echo "RaTOS SDK present but does not match ${_SDK_KEY} — re-extracting..."
    fi

    if [[ "$_SDK_STALE" -eq 1 ]]; then
        if [[ -z "$RATOS_RELEASE_REPO" || -z "$RATOS_RELEASE_TAG" ]]; then
            echo "ERROR: RaTOS SDK not found and RATOS_RELEASE_TAG/RATOS_RELEASE_REPO are not set." >&2
            echo "       Set them in .commrat.env or pass --tag <version>." >&2
            exit 1
        fi

        mkdir -p "$CACHE_DIR"
        # Download if not already in cache
        SDK_CACHE_FILE="${CACHE_DIR}/ratos-dev-sdk-container-amd64.xz"
        if [[ ! -f "$SDK_CACHE_FILE" ]]; then
            echo "Downloading RaTOS SDK for ${RATOS_RELEASE_TAG}..."
            gh release download "$RATOS_RELEASE_TAG" \
                --repo "$RATOS_RELEASE_REPO" \
                --pattern "ratos-dev-sdk-container-amd64*" \
                --dir "$CACHE_DIR" \
                --clobber
            SDK_CACHE_FILE="$(ls "$CACHE_DIR"/ratos-dev-sdk-container-amd64* | head -1)"
        else
            echo "Using cached SDK archive: ${SDK_CACHE_FILE}"
        fi

        echo "Extracting RaTOS SDK to ${EVL_SDK_DIR} ..."
        rm -rf "$EVL_SDK_DIR"
        mkdir -p "$EVL_SDK_DIR"
        # The sysroot tarball includes dev/ device nodes that cannot be
        # created without root. Those files are irrelevant for compilation;
        # suppress the errors and verify the headers/libs were extracted.
        tar -xJf "$SDK_CACHE_FILE" -C "$EVL_SDK_DIR" --strip-components=1 2>/dev/null || true
        if [[ ! -f "$EVL_SDK_DIR/usr/include/evl/evl.h" ]]; then
            echo "ERROR: SDK extraction failed — evl/evl.h not found in ${EVL_SDK_DIR}" >&2
            exit 1
        fi

        # Relocate: patch ELF interpreter paths in SDK binaries and update the
        # GCC sysroot wrapper to point at the extraction dir.
        # Requires patchelf on the host (apt install patchelf).
        if command -v patchelf &>/dev/null; then
            "$EVL_SDK_DIR/relocate-sdk.sh" 2>/dev/null || \
                sed -i "s|^GCC_SYSROOT=.*|GCC_SYSROOT=\"${EVL_SDK_DIR}\"|" \
                    "$EVL_SDK_DIR/usr/bin/gcc-sysroot-wrapper.sh"
        else
            echo "WARNING: patchelf not found — patching gcc wrapper manually." >&2
            sed -i "s|^GCC_SYSROOT=.*|GCC_SYSROOT=\"${EVL_SDK_DIR}\"|" \
                "$EVL_SDK_DIR/usr/bin/gcc-sysroot-wrapper.sh"
        fi

        [[ -n "$_SDK_KEY" ]] && echo "$_SDK_KEY" > "$SDK_KEY_FILE"
        echo "SDK extracted and relocated."
    else
        _relocate_sdk "$EVL_SDK_DIR"
        echo "Using RaTOS SDK at ${EVL_SDK_DIR} (${_SDK_KEY:-unversioned})"
    fi

    export EVL_SDK_DIR

    echo "Building with preset '${DO_CROSS}' against RaTOS SDK..."
    cmake --preset "${DO_CROSS}"
    cmake --build --preset "${DO_CROSS}" --parallel "$(nproc)"
    echo "Build complete."
fi

# ---------------------------------------------------------------------------
# Skip QEMU if no guest-side actions were requested (compile-only run).
# This applies when --cross is set but none of --test, --build, --run,
# or --shell were given. Useful for CI compile checks without QEMU overhead.
# ---------------------------------------------------------------------------
if [[ -n "$DO_CROSS" && -z "$DO_BUILD" && "$DO_TEST" -eq 0 \
      && ${#RUN_CMDS[@]} -eq 0 && "$DO_SHELL" -eq 0 && "$DO_CONSOLE" -eq 0 \
      && "$DO_GEN_DESCRIPTORS" -eq 0 ]]; then
    echo "Cross-compile complete. No guest actions requested — skipping QEMU."
    exit 0
fi

# ---------------------------------------------------------------------------
# Download artifacts with persistent cache in .evl-cache/
# ---------------------------------------------------------------------------

_do_download() {
    local dl_dir="$1" run_id="$2" release_tag="$3"

    mkdir -p "$dl_dir"

    if [[ -n "$run_id" ]]; then
        echo "Downloading artifacts from workflow run ${run_id}..."
        gh run download "$run_id" \
            --repo "$RATOS_RELEASE_REPO" \
            --name ratos-evl-artifacts \
            --dir "$dl_dir"
    else
        echo "Downloading artifacts from release tag ${release_tag}..."
        gh release download "$release_tag" \
            --repo "$RATOS_RELEASE_REPO" \
            --pattern "vmlinuz" \
            --pattern "initrd.img" \
            --pattern "ratos-sertial-image-container-amd64.ext4.gz" \
            --dir "$dl_dir"
    fi

    echo "Downloaded:"
    ls -lh "$dl_dir/"

    # Decompress ext4
    if compgen -G "$dl_dir/*.ext4.gz" > /dev/null; then
        local gz
        gz="$(ls "$dl_dir"/*.ext4.gz | head -1)"
        echo "Decompressing ${gz} ..."
        gunzip "$gz"
    fi
}

if [[ -z "$EXT4_PATH" || -z "$KERNEL_PATH" || -z "$INITRD_PATH" ]]; then
    # 1. ISAR directory (--images-dir / LOCAL_IMAGES_DIR + image name)
    _ISAR_DIR="${LOCAL_IMAGES_DIR:-}"
    _ISAR_NAME="${LOCAL_IMAGE_NAME:-}"
    if [[ -n "$_ISAR_DIR" ]]; then
        if [[ -z "$_ISAR_NAME" ]]; then
            # Auto-detect: list unique image name prefixes in the directory
            mapfile -t _available < <(
                ls "$_ISAR_DIR"/*.ext4 2>/dev/null | grep -v '\.wic\.' | \
                sed 's|-[^-]*-container.*||;s|.*/||' | sort -u || true)
            if [[ ${#_available[@]} -eq 1 ]]; then
                _ISAR_NAME="${_available[0]}"
                echo "Auto-selected image: ${_ISAR_NAME}"
            elif [[ ${#_available[@]} -eq 0 ]]; then
                echo "ERROR: No *.ext4 images found in ${_ISAR_DIR}" >&2; exit 1
            else
                echo "ERROR: Multiple images in ${_ISAR_DIR} — set LOCAL_IMAGE_NAME (or --image-name):" >&2
                printf '         %s\n' "${_available[@]}" >&2
                exit 1
            fi
        fi
        _resolve_isar_images "$_ISAR_DIR" "$_ISAR_NAME"
    fi
fi

if [[ -z "$EXT4_PATH" || -z "$KERNEL_PATH" || -z "$INITRD_PATH" ]]; then
    # 2. Explicit LOCAL_* env vars (single file paths)
    [[ -z "$EXT4_PATH"    && -n "$LOCAL_EXT4"    ]] && EXT4_PATH="$(realpath "$LOCAL_EXT4")"
    [[ -z "$KERNEL_PATH"  && -n "$LOCAL_KERNEL"  ]] && KERNEL_PATH="$(realpath "$LOCAL_KERNEL")"
    [[ -z "$INITRD_PATH"  && -n "$LOCAL_INITRD"  ]] && INITRD_PATH="$(realpath "$LOCAL_INITRD")"
fi

if [[ -z "$EXT4_PATH" || -z "$KERNEL_PATH" || -z "$INITRD_PATH" ]]; then
    if [[ -z "$RATOS_RELEASE_REPO" ]]; then
        echo "ERROR: RATOS_RELEASE_REPO is not set." >&2
        echo "       Set it in .commrat.env or export it before running this script." >&2
        echo "       Or pass --ext4, --kernel, --initrd to skip downloading." >&2
        exit 1
    fi

    # Resolve the target cache key before touching the cache.
    # Priority: --tag / RATOS_RELEASE_TAG (pinned in .commrat.env) >
    #           --run-id / RATOS_RUN_ID (explicit dev override) >
    #           latest successful run on main (fallback when no tag is pinned).
    CACHE_KEY=""
    if [[ -n "$RATOS_RELEASE_TAG" ]]; then
        CACHE_KEY="tag:${RATOS_RELEASE_TAG}"
    elif [[ -n "$RATOS_RUN_ID" ]]; then
        CACHE_KEY="run:${RATOS_RUN_ID}"
    else
        echo "No RATOS_RELEASE_TAG set — locating latest successful run on main..."
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
            echo "       Pass artifact paths, --run-id <id>, or --tag <tag>." >&2
            exit 1
        fi
        echo "Latest run ID: ${RATOS_RUN_ID}"
        CACHE_KEY="run:${RATOS_RUN_ID}"
    fi

    CACHED_KEY="${CACHE_DIR}/.cache_key"
    CACHED_EXT4="${CACHE_DIR}/ratos.ext4"
    CACHED_KERNEL="${CACHE_DIR}/vmlinuz"
    CACHED_INITRD="${CACHE_DIR}/initrd.img"

    if [[ -f "$CACHED_KEY" && "$(cat "$CACHED_KEY")" == "$CACHE_KEY" \
          && -f "$CACHED_EXT4" && -f "$CACHED_KERNEL" && -f "$CACHED_INITRD" ]]; then
        echo "Using cached artifacts ($CACHE_KEY)"
    else
        echo "Cache miss ($CACHE_KEY) — downloading..."
        # Remove only the artifact files, not the whole directory (SDK lives here too).
        # Also remove any leftover .ext4.gz from a prior partial download.
        rm -f "$CACHED_EXT4" "$CACHED_KERNEL" "$CACHED_INITRD" "$CACHED_KEY"
        rm -f "${CACHE_DIR}"/*.ext4.gz 2>/dev/null || true
        mkdir -p "$CACHE_DIR"

        if [[ "$CACHE_KEY" == tag:* ]]; then
            _do_download "$CACHE_DIR" "" "$RATOS_RELEASE_TAG"
        else
            _do_download "$CACHE_DIR" "$RATOS_RUN_ID" ""
        fi

        # Normalise filenames so the cache location is always predictable
        [[ -f "$CACHED_EXT4" ]]    || mv "$(ls "$CACHE_DIR"/*.ext4    | head -1)" "$CACHED_EXT4"
        [[ -f "$CACHED_KERNEL" ]]  || mv "$(ls "$CACHE_DIR"/*vmlinuz  | head -1)" "$CACHED_KERNEL"
        [[ -f "$CACHED_INITRD" ]]  || mv "$(ls "$CACHE_DIR"/*initrd*  | head -1)" "$CACHED_INITRD"

        echo "$CACHE_KEY" > "$CACHED_KEY"
        echo "Artifacts cached in ${CACHE_DIR}/"
    fi

    EXT4_PATH="$CACHED_EXT4"
    KERNEL_PATH="$CACHED_KERNEL"
    INITRD_PATH="$CACHED_INITRD"
fi

echo "Using ext4 image : $EXT4_PATH"
echo "Using kernel     : $KERNEL_PATH"
echo "Using initrd     : $INITRD_PATH"

# ---------------------------------------------------------------------------
# Copy ext4 image so the cache is never modified
# ---------------------------------------------------------------------------
EXT4_COPY="$WORK_DIR/ratos.ext4"
echo "Copying ext4 image to $EXT4_COPY ..."
case "$EXT4_PATH" in
    *.gz|*.xz|*.zst)
        EXT4_PATH="$(_decompress_artifact "$EXT4_PATH" "ratos_decompressed.ext4")"
        ;;
esac
cp "$EXT4_PATH" "$EXT4_COPY"
# Grow the working copy by 256 MB so cross-compiled binaries fit.
truncate -s "+256M" "$EXT4_COPY"
resize2fs "$EXT4_COPY" 2>/dev/null

# ---------------------------------------------------------------------------
# Generate ephemeral SSH keypair
# ---------------------------------------------------------------------------
SSH_KEY="$WORK_DIR/ci_key"
ssh-keygen -t ed25519 -N "" -f "$SSH_KEY" -q

# ---------------------------------------------------------------------------
# Inject public key into the image (simple loop mount — no kpartx needed)
# ---------------------------------------------------------------------------
echo "Injecting SSH public key into guest image..."
MOUNT_POINT="$WORK_DIR/mnt"
mkdir -p "$MOUNT_POINT"

sudo mount -o loop "$EXT4_COPY" "$MOUNT_POINT"
touch "$WORK_DIR/loop_mounted"
sudo mkdir -p "$MOUNT_POINT/root/.ssh"
sudo cp "${SSH_KEY}.pub" "$MOUNT_POINT/root/.ssh/authorized_keys"
sudo chmod 700 "$MOUNT_POINT/root/.ssh"
sudo chmod 600 "$MOUNT_POINT/root/.ssh/authorized_keys"
# Remove CPU time limit set by guest /etc/profile (SIGXCPU kills long-running tests).
printf 'ulimit -H -t unlimited 2>/dev/null || true\nulimit -t unlimited 2>/dev/null || true\n' \
    | sudo tee "$MOUNT_POINT/etc/profile.d/no-cpu-limit.sh" > /dev/null
sudo chmod 644 "$MOUNT_POINT/etc/profile.d/no-cpu-limit.sh"
sudo umount "$MOUNT_POINT"
rm -f "$WORK_DIR/loop_mounted"

# ---------------------------------------------------------------------------
# Boot QEMU
# ---------------------------------------------------------------------------
# -cpu host -enable-kvm gives the guest full host CPU features (required for
# good RT performance). Fall back to -cpu qemu64 without KVM when unavailable.
if [[ -w /dev/kvm ]]; then
    CPU_ARGS="-cpu host -enable-kvm"
    echo "KVM acceleration enabled."
else
    CPU_ARGS="-cpu qemu64"
    echo "WARNING: /dev/kvm not accessible — running without KVM (slow)." >&2
    echo "         Add yourself to the 'kvm' group: sudo usermod -aG kvm \$USER" >&2
fi

# --console mode: run QEMU in the foreground with serial console on stdio.
# Press Ctrl+A X to quit QEMU, or Ctrl+A C to enter the QEMU monitor.
if [[ "$DO_CONSOLE" -eq 1 ]]; then
    echo "Starting QEMU with serial console (Ctrl+A X to quit)..."
    # shellcheck disable=SC2086
    exec qemu-system-x86_64 \
        ${CPU_ARGS} \
        -smp "$QEMU_CPUS" \
        -m "$QEMU_MEMORY" \
        -machine q35 \
        -kernel "$KERNEL_PATH" \
        -initrd "$INITRD_PATH" \
        -drive "file=${EXT4_COPY},discard=unmap,if=none,id=disk,format=raw" \
        -device ide-hd,drive=disk \
        -append "root=/dev/sda rw rootwait console=ttyS0" \
        -nic "user,hostfwd=tcp:127.0.0.1:${SSH_PORT}-:22,model=e1000" \
        -device virtio-rng-pci \
        -serial mon:stdio \
        -nographic
fi

# Kill any stale QEMU that might still be holding our SSH port
pkill -f "hostfwd=tcp:127.0.0.1:${SSH_PORT}-:22" 2>/dev/null || true

echo "Starting QEMU..."
# shellcheck disable=SC2086
qemu-system-x86_64 \
    ${CPU_ARGS} \
    -smp "$QEMU_CPUS" \
    -m "$QEMU_MEMORY" \
    -machine q35 \
    -kernel "$KERNEL_PATH" \
    -initrd "$INITRD_PATH" \
    -drive "file=${EXT4_COPY},discard=unmap,if=none,id=disk,format=raw" \
    -device ide-hd,drive=disk \
    -append "root=/dev/sda rw rootwait console=ttyS0" \
    -nic "user,hostfwd=tcp:127.0.0.1:${SSH_PORT}-:22,model=e1000" \
    -device virtio-rng-pci \
    -serial "file:${WORK_DIR}/qemu-serial.log" \
    -monitor none \
    -nographic \
    < /dev/null \
    > "$WORK_DIR/qemu.log" 2>&1 &
echo $! > "$WORK_DIR/qemu.pid"

# ---------------------------------------------------------------------------
# Wait for SSH to become available
# ---------------------------------------------------------------------------
echo "Waiting for guest SSH (up to 3 minutes)..."
SSH_OPTS="-i ${SSH_KEY} -o StrictHostKeyChecking=no -o BatchMode=yes -o ConnectTimeout=10 -o UserKnownHostsFile=/dev/null -p ${SSH_PORT}"
for i in $(seq 1 18); do
    if ! kill -0 "$(cat "$WORK_DIR/qemu.pid")" 2>/dev/null; then
        echo "ERROR: QEMU process died unexpectedly!" >&2
        tail -20 "$WORK_DIR/qemu.log" >&2
        exit 1
    fi
    if ssh ${SSH_OPTS} root@127.0.0.1 true 2>/dev/null; then
        echo "SSH ready after ~$((i * 10))s"
        break
    fi
    if [[ "$i" -eq 18 ]]; then
        echo "ERROR: Timeout waiting for SSH after 3 minutes" >&2
        tail -20 "$WORK_DIR/qemu.log" >&2
        tail -20 "$WORK_DIR/qemu-serial.log" >&2
        exit 1
    fi
    echo "Waiting for SSH... (~$((i * 10))s)"
done

# ---------------------------------------------------------------------------
# Execute requested actions: cross-deploy -> build -> test -> run -> shell
# ---------------------------------------------------------------------------

# Deploy cross-compiled binaries to guest.
# If no --cross/--build given but --test or --shell requested, auto-detect a cached
# in-guest build (build/evl/) or cross-compile output (build/evl-cross/).
if [[ -z "$DO_CROSS" && -z "$DO_BUILD" && ( "$DO_SHELL" -eq 1 || "$DO_TEST" -eq 1 ) ]]; then
    for _preset in evl evl-cross; do
        if [[ -d "build/${_preset}/test" ]]; then
            echo "Note: Using cached build/${_preset}/ (no --build/--cross specified)."
            DO_CROSS="${_preset}"
            break
        fi
    done
fi
if [[ -n "$DO_CROSS" && ( "$DO_SHELL" -eq 1 || ${#RUN_CMDS[@]} -gt 0 || "$DO_GEN_DESCRIPTORS" -eq 1 ) ]]; then
    echo "Deploying build/${DO_CROSS}/ to guest /root/commrat/..."
    rsync -az --delete \
        -e "ssh ${SSH_OPTS}" \
        "build/${DO_CROSS}/" "root@127.0.0.1:/root/commrat/"
fi

# Build inside guest (rsync source first)
if [[ -n "$DO_BUILD" ]]; then
    echo "Transferring CommRaT source to guest..."
    rsync -az --delete \
        --exclude=build \
        --exclude=.git \
        --exclude=.evl-cache \
        -e "ssh ${SSH_OPTS}" \
        ./ "root@127.0.0.1:/root/CommRaT/"

    echo "Building with preset '${DO_BUILD}' inside EVL guest..."
    ssh ${SSH_OPTS} root@127.0.0.1 bash -lc "
        set -euo pipefail
        cd /root/CommRaT
        cmake --preset ${DO_BUILD} 2>&1
        cmake --build --preset ${DO_BUILD} --parallel \$(( \$(nproc) < 2 ? 1 : 2 )) 2>&1
        echo 'Build complete.'
    "
    # Sync built binaries back to host so subsequent --test runs skip recompilation.
    echo "Caching build/${DO_BUILD}/ from guest to host..."
    mkdir -p "build/${DO_BUILD}"
    rsync -az \
        -e "ssh ${SSH_OPTS}" \
        "root@127.0.0.1:/root/CommRaT/build/${DO_BUILD}/" "build/${DO_BUILD}/"
    # Treat as a binary cache for --test/--shell/--run steps below.
    DO_CROSS="${DO_BUILD}"
fi

# Run tests
if [[ "$DO_TEST" -eq 1 ]]; then
    # Determine ctest preset: use DO_BUILD preset if we just built, otherwise evl.
    CTEST_PRESET="${DO_BUILD:-evl}"

    if [[ -z "$DO_BUILD" && -n "$DO_CROSS" ]]; then
        # Binary cache case: source not on guest yet.
        # Patch CTestTestfile.cmake on the host to replace host paths with guest
        # paths, then deploy. This avoids running cmake on the guest (which would
        # require all deps to be installed there).
        echo "Deploying source to guest /root/CommRaT/ ..."
        rsync -az --delete \
            --exclude=build --exclude=.git --exclude=.evl-cache \
            -e "ssh ${SSH_OPTS}" \
            ./ "root@127.0.0.1:/root/CommRaT/"

        echo "Patching build paths in CTestTestfile.cmake for guest (host-side)..."
        _host_build_dir="$(realpath "${REPO_ROOT}/build/${DO_CROSS}")"
        find "${REPO_ROOT}/build/${DO_CROSS}" -name 'CTestTestfile.cmake' \
            -exec sed -i \
                -e "s|${_host_build_dir}|/root/CommRaT/build/evl|g" \
                -e "s|${REPO_ROOT}|/root/CommRaT|g" \
            '{}' '+'

        echo "Deploying build/${DO_CROSS}/ to guest /root/CommRaT/build/evl/ ..."
        ssh ${SSH_OPTS} root@127.0.0.1 mkdir -p "/root/CommRaT/build/evl"
        rsync -az \
            -e "ssh ${SSH_OPTS}" \
            "build/${DO_CROSS}/" "root@127.0.0.1:/root/CommRaT/build/evl/"
    fi

    # Run ctest against the deployed build tree.
    if [[ -n "$DO_BUILD" ]]; then
        # In-guest build: cmake ran on guest so ctest preset is valid.
        echo "Running ctest --preset ${CTEST_PRESET} on EVL guest..."
        ssh ${SSH_OPTS} root@127.0.0.1 bash -lc "
            set -euo pipefail
            cd /root/CommRaT
            ctest --preset ${CTEST_PRESET} --output-on-failure --timeout 120 2>&1
        "
    else
        # Cross-compile: paths were patched above; use --test-dir directly.
        # Lift the guest CPU-time ulimit inline so EVL threads don't die with
        # SIGXCPU. Using a multi-line bash -c (not bash -lc) avoids the SSH
        # argument-joining bug where "bash -lc <single-line>" collapses the
        # quoted command string into bare words, stripping --test-dir.
        echo "Running ctest on EVL guest (/root/CommRaT/build/evl)..."
        ssh ${SSH_OPTS} root@127.0.0.1 bash -c "
            ulimit -H -t unlimited 2>/dev/null || true
            ulimit -t unlimited 2>/dev/null || true
            ctest --test-dir /root/CommRaT/build/evl --output-on-failure --timeout 120
        "
    fi
fi

# Run specific binaries
for cmd in "${RUN_CMDS[@]}"; do
    echo "Running on EVL guest: ${cmd}"
    # Use bash -c with multi-line heredoc to avoid SSH argument-joining bug
    # where "bash -lc <single-line>" collapses the quoted string into bare words.
    ssh ${SSH_OPTS} root@127.0.0.1 bash -c "
        ulimit -H -t unlimited 2>/dev/null || true
        ulimit -t unlimited 2>/dev/null || true
        set -euo pipefail
        ${cmd}
    "
done

# Generate module descriptors: run --commrat-inspect on every module binary
# inside the guest (all in one QEMU session), then rsync .module.json back.
if [[ "$DO_GEN_DESCRIPTORS" -eq 1 ]]; then
    echo "Running --commrat-inspect on all module binaries in QEMU..."
    ssh ${SSH_OPTS} root@127.0.0.1 bash -c '
        set +e
        find /root/commrat -name "*.module.json" | while IFS= read -r json; do
            module_class=$(grep -o "\"module_class\"[[:space:]]*:[[:space:]]*\"[^\"]*\"" "$json" \
                           | grep -o "\"[^\"]*\"$" | tr -d "\"")
            binary_host=$(grep -o "\"binary\"[[:space:]]*:[[:space:]]*\"[^\"]*\"" "$json" \
                          | grep -o "\"[^\"]*\"$" | tr -d "\"")
            binary_name="${binary_host##*/}"
            binary=$(find /root/commrat -name "$binary_name" -type f 2>/dev/null | head -1)
            if [ -n "$binary" ] && [ -x "$binary" ]; then
                "$binary" --commrat-inspect "$json" "$module_class" "$binary" 2>/dev/null
            fi
        done
    '
    echo "Syncing descriptors back to host build/${DO_CROSS}/..."
    rsync -az --include="*/" --include="*.module.json" --exclude="*" \
        -e "ssh ${SSH_OPTS}" \
        "root@127.0.0.1:/root/commrat/" "build/${DO_CROSS}/"
    echo "EVL descriptors updated."
fi

# Interactive shell
if [[ "$DO_SHELL" -eq 1 ]]; then
    echo ""
    echo "Opening interactive EVL guest shell."
    if [[ -n "$DO_CROSS" ]]; then
        echo "Binaries are at /root/commrat/"
        echo "  evl ps          check EVL thread status"
        echo "  evl check       verify RT health"
    fi
    echo "Type 'exit' to stop QEMU and clean up."
    echo ""
    ssh -t ${SSH_OPTS} root@127.0.0.1 'ulimit -H -t unlimited 2>/dev/null; ulimit -t unlimited 2>/dev/null; exec bash --login'
fi
