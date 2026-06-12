#!/bin/bash
# SiliconV — AOSP GSI Download Script
#
# Downloads AOSP Generic System Image (GSI) for ARM64.
# Supports Android 14 and 15, with automatic CI artifact resolution.
#
# Usage: ./scripts/download_gsi.sh [variant] [android_version]
#   variant: user, userdebug (default: userdebug)
#   android_version: 14, 15 (default: 14)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
DOWNLOAD_DIR="${PROJECT_DIR}/build/gsi"
VARIANT="${1:-userdebug}"
ANDROID_VER="${2:-14}"

echo "=== SiliconV AOSP GSI Downloader ==="
echo "Android: ${ANDROID_VER}"
echo "Variant: ${VARIANT}"
echo "Target:  ${DOWNLOAD_DIR}"
echo ""

mkdir -p "${DOWNLOAD_DIR}"

# ── Check for existing images ───────────────────────

if [ -f "${DOWNLOAD_DIR}/system.img" ] && [ -f "${DOWNLOAD_DIR}/boot.img" ]; then
    echo "GSI images already present:"
    echo "  system.img: $(du -h "${DOWNLOAD_DIR}/system.img" | cut -f1)"
    echo "  boot.img:   $(du -h "${DOWNLOAD_DIR}/boot.img" | cut -f1)"
    echo ""
    echo "To re-download, delete: ${DOWNLOAD_DIR}/"
    exit 0
fi

# ── Download functions ──────────────────────────────

download_from_ci() {
    # Try to get build info from ci.android.com API
    local BRANCH="$1"
    local TARGET="$2"

    echo "Querying ci.android.com for ${BRANCH} / ${TARGET}..."

    # Get the latest green build number
    local STATUS_URL="https://ci.android.com/builds/branches/${BRANCH}/status"
    local BUILD_NUM

    BUILD_NUM=$(curl -fsSL --connect-timeout 10 "${STATUS_URL}" 2>/dev/null | \
        python3 -c "
import sys, json
try:
    data = json.load(sys.stdin)
    # Find the latest passing build
    for build in data.get('builds', []):
        if build.get('status') == 'passed':
            print(build.get('buildNumber', ''))
            break
except:
    pass
" 2>/dev/null || true)

    if [ -z "${BUILD_NUM}" ]; then
        echo "  ✗ Could not determine build number from CI API"
        return 1
    fi

    echo "  ✓ Latest green build: ${BUILD_NUM}"

    # Download target files
    local ARTIFACT_URL="https://ci.android.com/builds/submitted/${BUILD_NUM}/${TARGET}/latest/raw/"
    local ZIP_NAME="aosp_${TARGET}-ota-${BUILD_NUM}.zip"

    echo "  Downloading ${ZIP_NAME}..."
    curl -fSL --connect-timeout 30 --progress-bar \
        -o "${DOWNLOAD_DIR}/${ZIP_NAME}" \
        "${ARTIFACT_URL}" 2>/dev/null || {
        echo "  ✗ Download failed"
        rm -f "${DOWNLOAD_DIR}/${ZIP_NAME}"
        return 1
    }

    # Extract system.img and boot.img
    echo "  Extracting images..."
    cd "${DOWNLOAD_DIR}"

    # Try to extract from the zip
    if command -v unzip &>/dev/null; then
        unzip -o "${ZIP_NAME}" "system.img" "boot.img" 2>/dev/null || true
    fi

    # Clean up zip
    rm -f "${ZIP_NAME}"
    cd "${PROJECT_DIR}"
    return 0
}

download_from_google() {
    # Direct download from Google's GSI release page
    local VER="$1"
    local ARCH="arm64"

    echo "Attempting direct download from Google GSI releases..."

    # Android 14 GSI direct URLs (Vanilla Ice Cream / VF = Vanilla Full)
    # These URLs are from https://developer.android.com/topic/generic-system-image/releases
    local GSI_ZIP=""

    case "${VER}" in
        14)
            # Android 14 QPR2 GSI
            GSI_ZIP="https://dl.google.com/dl/android/aosp/arm64_vf/aosp_arm64_vf-userdebug-14.zip"
            ;;
        15)
            GSI_ZIP="https://dl.google.com/dl/android/aosp/arm64_vf/aosp_arm64_vf-userdebug-15.zip"
            ;;
    esac

    if [ -n "${GSI_ZIP}" ]; then
        echo "  Downloading from: ${GSI_ZIP}"
        curl -fSL --connect-timeout 30 --progress-bar \
            -o "${DOWNLOAD_DIR}/gsi.zip" \
            "${GSI_ZIP}" 2>/dev/null && {

            cd "${DOWNLOAD_DIR}"
            # Extract
            if command -v unzip &>/dev/null; then
                unzip -o "gsi.zip" 2>/dev/null || true
                # Rename if needed
                [ -f "system-qemu.img" ] && mv "system-qemu.img" "system.img" 2>/dev/null || true
                [ -f "vendor-qemu.img" ] && mv "vendor-qemu.img" "vendor.img" 2>/dev/null || true
            fi
            rm -f "gsi.zip"
            cd "${PROJECT_DIR}"
            return 0
        } || {
            echo "  ✗ Direct download failed"
            rm -f "${DOWNLOAD_DIR}/gsi.zip"
        }
    fi

    return 1
}

# ── Main download logic ─────────────────────────────

echo "Attempting download (method 1: Google CI API)..."

BRANCH="aosp-main"
TARGET="aosp_arm64-${VARIANT}"

if ! download_from_ci "${BRANCH}" "${TARGET}"; then
    echo ""
    echo "Attempting download (method 2: Google GSI releases)..."

    if ! download_from_google "${ANDROID_VER}"; then
        echo ""
        echo "========================================="
        echo "  Automatic download failed."
        echo "========================================="
        echo ""
        echo "Please manually download AOSP GSI:"
        echo ""
        echo "  Android ${ANDROID_VER} ARM64 ${VARIANT} GSI:"
        echo "  https://developer.android.com/topic/generic-system-image/releases"
        echo ""
        echo "Or from ci.android.com:"
        echo "  https://ci.android.com/builds/branches/aosp-main/status"
        echo ""
        echo "Required files (place in ${DOWNLOAD_DIR}/):"
        echo "  1. system.img  — Main system partition"
        echo "  2. boot.img   — Kernel + ramdisk (we extract ramdisk)"
        echo ""
        echo "Optional:"
        echo "  3. vbmeta.img — Verified boot metadata"
        echo ""
        exit 1
    fi
fi

# ── Verify downloaded images ───────────────────────

echo ""
echo "Verifying downloaded images..."

OK=true

if [ -f "${DOWNLOAD_DIR}/system.img" ]; then
    SIZE=$(du -h "${DOWNLOAD_DIR}/system.img" | cut -f1)
    echo "  ✓ system.img (${SIZE})"
else
    echo "  ✗ system.img not found"
    OK=false
fi

if [ -f "${DOWNLOAD_DIR}/boot.img" ]; then
    SIZE=$(du -h "${DOWNLOAD_DIR}/boot.img" | cut -f1)
    echo "  ✓ boot.img (${SIZE})"
else
    echo "  ✗ boot.img not found (needed for ramdisk extraction)"
fi

if [ "${OK}" = true ]; then
    echo ""
    echo "=== GSI Download Complete ==="
    echo ""
    echo "Next steps:"
    echo "  1. Build Android images: ./scripts/build_android_images.sh"
    echo "  2. Test Android boot:     ./scripts/test_android_qemu.sh"
else
    echo ""
    echo "WARNING: Some images are missing. See manual download instructions above."
fi
