#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
#
# SiliconV — GSI Ramdisk Extraction & fstab Injection
#
# Extracts the ramdisk from a GSI boot.img, injects fstab.siliconv into
# the appropriate location for System-as-Root first-stage mounting, and
# repacks it for use as the kernel initramfs.
#
# Usage: ./scripts/extract_gsi_ramdisk.sh
#
# Prerequisites:
#   - build/gsi/boot.img (from download_gsi.sh)
#   - mkbootimg/unpackbootimg or Python for manual boot.img parsing
#   - cpio, gzip, lz4

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

GSI_DIR="${PROJECT_DIR}/build/gsi"
FSTAB_SRC="${PROJECT_DIR}/android/init/fstab.siliconv"
BOOT_IMG="${GSI_DIR}/boot.img"
OUTPUT="${PROJECT_DIR}/build/android_initramfs.cpio.gz"

echo "=== SiliconV GSI Ramdisk Extraction ==="
echo ""

# ── Step 1: Check prerequisites ──────────────────

if [ ! -f "${BOOT_IMG}" ]; then
    echo "ERROR: ${BOOT_IMG} not found"
    echo "Run ./scripts/download_gsi.sh first."
    exit 1
fi

if [ ! -f "${FSTAB_SRC}" ]; then
    echo "ERROR: ${FSTAB_SRC} not found"
    echo "Run the SiliconV build to generate fstab first."
    exit 1
fi

mkdir -p "${PROJECT_DIR}/build"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "${WORK_DIR}"' EXIT

echo "Boot image: ${BOOT_IMG}"
echo "Fstab:      ${FSTAB_SRC}"
echo "Work dir:   ${WORK_DIR}"
echo ""

# ── Step 2: Extract ramdisk from boot.img ─────────

RAMDISK_FILE=""

# Method 1: Try unpackbootimg (from AOSP / mkbootimg tools)
if command -v unpackbootimg &>/dev/null; then
    echo "Extracting ramdisk with unpackbootimg..."
    if unpackbootimg -i "${BOOT_IMG}" -o "${WORK_DIR}/boot_unpacked" 2>/dev/null; then
        # unpackbootimg typically outputs ramdisk as boot.img-ramdisk
        for candidate in \
            "${WORK_DIR}/boot_unpacked/boot.img-ramdisk" \
            "${WORK_DIR}/boot_unpacked/ramdisk"; do
            if [ -f "${candidate}" ]; then
                RAMDISK_FILE="${candidate}"
                break
            fi
        done
    fi
fi

# Method 2: Try mkbootimg's unpack_bootimg.py
if [ -z "${RAMDISK_FILE}" ] && command -v python3 &>/dev/null; then
    UNPACK_SCRIPT=""
    for path in \
        "${PROJECT_DIR}/third_party/mkbootimg/unpack_bootimg.py" \
        "/usr/share/mkbootimg/unpack_bootimg.py"; do
        if [ -f "${path}" ]; then
            UNPACK_SCRIPT="${path}"
            break
        fi
    done

    if [ -n "${UNPACK_SCRIPT}" ]; then
        echo "Extracting ramdisk with unpack_bootimg.py..."
        if python3 "${UNPACK_SCRIPT}" \
                --boot_img "${BOOT_IMG}" \
                --out "${WORK_DIR}/boot_unpacked" 2>/dev/null; then
            for candidate in \
                "${WORK_DIR}/boot_unpacked/ramdisk" \
                "${WORK_DIR}/boot_unpacked/ramdisk.lz4" \
                "${WORK_DIR}/boot_unpacked/ramdisk.gz"; do
                if [ -f "${candidate}" ]; then
                    RAMDISK_FILE="${candidate}"
                    break
                fi
            done
        fi
    fi
fi

# Method 3: Manual boot.img header parsing
# Android boot image header v0-v4: ramdisk starts at page-aligned offset
# after kernel. We parse the header to find offsets.
if [ -z "${RAMDISK_FILE}" ]; then
    echo "Manual boot.img parsing (no unpackbootimg found)..."

    # Parse Android boot image header (little-endian)
    # Offset 0: magic "ANDROID!" (8 bytes)
    # Offset 8: kernel_size (4 bytes)
    # Offset 12: kernel_addr (4 bytes)
    # Offset 16: ramdisk_size (4 bytes)
    # Offset 20: ramdisk_addr (4 bytes)
    # Page size is at offset 36

    MAGIC=$(dd if="${BOOT_IMG}" bs=1 count=8 2>/dev/null | xxd -p)
    if [ "${MAGIC}" != "414e44524f494421" ]; then
        echo "ERROR: Not a valid Android boot image (bad magic)"
        exit 1
    fi

    # Read kernel_size and ramdisk_size (little-endian uint32)
    KERNEL_SIZE=$(dd if="${BOOT_IMG}" bs=1 skip=8 count=4 2>/dev/null | od -An -tu4 | tr -d ' ')
    RAMDISK_SIZE=$(dd if="${BOOT_IMG}" bs=1 skip=16 count=4 2>/dev/null | od -An -tu4 | tr -d ' ')
    PAGE_SIZE=$(dd if="${BOOT_IMG}" bs=1 skip=36 count=4 2>/dev/null | od -An -tu4 | tr -d ' ')

    # Default page size
    PAGE_SIZE=${PAGE_SIZE:-4096}

    echo "  kernel_size:  ${KERNEL_SIZE}"
    echo "  ramdisk_size: ${RAMDISK_SIZE}"
    echo "  page_size:    ${PAGE_SIZE}"

    if [ "${RAMDISK_SIZE}" -eq 0 ]; then
        echo "ERROR: boot.img contains no ramdisk"
        exit 1
    fi

    # Calculate ramdisk offset: page-aligned after kernel pages
    KERNEL_PAGES=$(( (KERNEL_SIZE + PAGE_SIZE - 1) / PAGE_SIZE ))
    RAMDISK_OFFSET=$(( (1 + KERNEL_PAGES) * PAGE_SIZE ))

    echo "  ramdisk_offset: ${RAMDISK_OFFSET}"

    dd if="${BOOT_IMG}" of="${WORK_DIR}/ramdisk.raw" \
       bs=1 skip="${RAMDISK_OFFSET}" count="${RAMDISK_SIZE}" 2>/dev/null
    RAMDISK_FILE="${WORK_DIR}/ramdisk.raw"
fi

if [ -z "${RAMDISK_FILE}" ] || [ ! -f "${RAMDISK_FILE}" ]; then
    echo "ERROR: Could not extract ramdisk from boot.img"
    exit 1
fi

echo "Ramdisk extracted: ${RAMDISK_FILE} ($(du -h "${RAMDISK_FILE}" | cut -f1))"
echo ""

# ── Step 3: Decompress ramdisk ───────────────────

RAMDISK_CPIO="${WORK_DIR}/ramdisk.cpio"

# Detect compression format
FILE_MAGIC=$(xxd -l 4 -p "${RAMDISK_FILE}" 2>/dev/null || true)

case "${FILE_MAGIC}" in
    1f8b*)   # gzip
        echo "Decompressing gzip ramdisk..."
        zcat "${RAMDISK_FILE}" > "${RAMDISK_CPIO}"
        ;;
    04224d18)  # lz4
        echo "Decompressing lz4 ramdisk..."
        if command -v lz4 &>/dev/null; then
            lz4 -d "${RAMDISK_FILE}" "${RAMDISK_CPIO}"
        else
            echo "ERROR: lz4 tool not found, install: apt install lz4"
            exit 1
        fi
        ;;
    30373037*)  # cpio magic "070701" in hex = 30373037
        echo "Ramdisk is uncompressed cpio..."
        cp "${RAMDISK_FILE}" "${RAMDISK_CPIO}"
        ;;
    *)
        # Try gzip first (most common), then lz4
        echo "Unknown format (magic: ${FILE_MAGIC}), trying gzip..."
        if zcat "${RAMDISK_FILE}" > "${RAMDISK_CPIO}" 2>/dev/null; then
            echo "  gzip decompression succeeded"
        else
            echo "Trying lz4..."
            if command -v lz4 &>/dev/null && lz4 -d "${RAMDISK_FILE}" "${RAMDISK_CPIO}" 2>/dev/null; then
                echo "  lz4 decompression succeeded"
            else
                echo "ERROR: Could not decompress ramdisk"
                exit 1
            fi
        fi
        ;;
esac

# ── Step 4: Unpack cpio and inject fstab ─────────

INITRAMFS_DIR="${WORK_DIR}/initramfs"
mkdir -p "${INITRAMFS_DIR}"

echo "Unpacking cpio archive..."
cd "${INITRAMFS_DIR}"
cpio -i -d < "${RAMDISK_CPIO}" 2>/dev/null || true
cd "${PROJECT_DIR}"

# Inject fstab.siliconv into the correct location for System-as-Root:
# - /first_stage_ramdisk/vendor/etc/fstab.siliconv (AOSP first-stage mount)
# - /vendor/etc/fstab.siliconv (fallback)
FSTAB_DIRS=(
    "${INITRAMFS_DIR}/first_stage_ramdisk/vendor/etc"
    "${INITRAMFS_DIR}/vendor/etc"
)

for dir in "${FSTAB_DIRS[@]}"; do
    mkdir -p "${dir}"
    cp "${FSTAB_SRC}" "${dir}/fstab.siliconv"
    echo "Injected fstab.siliconv -> ${dir#"${INITRAMFS_DIR}"}/fstab.siliconv"
done

# ── Step 5: Repack into cpio.gz ───────────────────

echo "Repacking initramfs..."
cd "${INITRAMFS_DIR}"
find . | cpio -H newc -o --quiet | gzip > "${OUTPUT}"
cd "${PROJECT_DIR}"

SIZE=$(stat -c%s "${OUTPUT}")
echo ""
echo "=== Done ==="
echo "Output: ${OUTPUT}"
echo "Size:   ${SIZE} bytes ($(numfmt --to=iec "${SIZE}" 2>/dev/null || echo "${SIZE}"))"
