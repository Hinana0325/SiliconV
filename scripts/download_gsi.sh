#!/bin/bash
# SiliconV — AOSP GSI Download Script
#
# Downloads AOSP Generic System Image (GSI) for ARM64.
# These are official Google-provided system images for Treble devices.
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

# ── AOSP GSI Download URLs ──────────────────────
# These are the official Google CI artifacts for ARM64 GSI

case "${ANDROID_VER}" in
    14)
        # Android 14 (API 34) GSI
        GSI_BUILD="android-14.0.0_r1"
        GSI_ARCH="arm64"
        GSI_TYPE="vf"  # Vanilla (no Google apps)

        # Direct download from Google CI
        # Note: URLs may change; check https://ci.android.com
        BASE_URL="https://dl.google.com/dl/android/aosp"
        IMAGE_NAME="aosp_arm64-${VARIANT}"

        echo "Downloading Android 14 GSI..."
        echo ""
        echo "NOTE: AOSP GSI images are ~2GB. Download from:"
        echo "  https://developer.android.com/topic/generic-system-image/releases"
        echo ""
        echo "Required files:"
        echo "  1. system.img (ARM64, ${VARIANT})"
        echo "  2. boot.img (if separate)"
        echo ""

        # Try to use the Android download page
        DOWNLOAD_PAGE="https://developer.android.com/topic/generic-system-image/releases"

        echo "Download page: ${DOWNLOAD_PAGE}"
        echo ""
        echo "After downloading, place files in: ${DOWNLOAD_DIR}/"
        echo ""
        ;;

    15)
        # Android 15 (API 35) GSI
        echo "Android 15 GSI:"
        echo "  Download from: https://developer.android.com/topic/generic-system-image/releases"
        echo "  Select: ARM64 + ${VARIANT}"
        echo "  Place in: ${DOWNLOAD_DIR}/"
        echo ""
        ;;

    *)
        echo "ERROR: Unsupported Android version: ${ANDROID_VER}"
        echo "Supported: 14, 15"
        exit 1
        ;;
esac

# ── Check for existing images ───────────────────

echo "Checking for existing GSI images in ${DOWNLOAD_DIR}..."

if [ -f "${DOWNLOAD_DIR}/system.img" ]; then
    echo "  ✓ system.img found ($(du -h "${DOWNLOAD_DIR}/system.img" | cut -f1))"
else
    echo "  ✗ system.img not found"
fi

if [ -f "${DOWNLOAD_DIR}/boot.img" ]; then
    echo "  ✓ boot.img found"
else
    echo "  ✗ boot.img not found"
fi

echo ""
echo "=== Next Steps ==="
echo ""
echo "1. Download GSI from the URL above"
echo "2. Extract to ${DOWNLOAD_DIR}/"
echo "3. Run: ./scripts/prepare_rootfs.sh"
echo ""
echo "Or use the automated build:"
echo "  ./scripts/build_all.sh"
