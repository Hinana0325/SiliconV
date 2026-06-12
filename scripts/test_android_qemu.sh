#!/bin/bash
# SiliconV — AOSP Android Boot Test in QEMU
#
# Boots AOSP GSI with SiliconV kernel in QEMU (ARM64 software emulation).
# Verifies Android init milestones: FirstStageMount -> SELinux -> SecondStage
# -> servicemanager -> logd -> logcat
#
# Usage: ./scripts/test_android_qemu.sh [mode]
#   mode: full (default), quick (60s timeout), debug (interactive)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

# ── Configuration ───────────────────────────────────

KERNEL_IMAGE="${KERNEL_IMAGE:-${PROJECT_DIR}/kernel/out/Image}"
SYSTEM_IMG="${SYSTEM_IMG:-${PROJECT_DIR}/build/android_images/system.img}"
VENDOR_IMG="${VENDOR_IMG:-${PROJECT_DIR}/build/android_images/vendor.img}"
DATA_IMG="${DATA_IMG:-${PROJECT_DIR}/build/android_images/userdata.img}"
CACHE_IMG="${CACHE_IMG:-${PROJECT_DIR}/build/android_images/cache.img}"
META_IMG="${META_IMG:-${PROJECT_DIR}/build/android_images/metadata.img}"
INITRAMFS="${INITRAMFS:-${PROJECT_DIR}/build/android_initramfs.cpio.gz}"

MODE="${1:-full}"
QEMU_CMD="${QEMU_CMD:-qemu-system-aarch64}"

# ── Android kernel command line ────────────────────
# Critical parameters for AOSP init:
#   androidboot.hardware=siliconv — init uses this to find fstab.siliconv
#   androidboot.selinux=permissive — load SELinux policy in permissive mode
#   androidboot.veritymode=disabled — skip dm-verity on /system
#   androidboot.force_normal_boot=1 — skip recovery, boot normally
#   androidboot.debuggable=1 — enable ADB and debug features

ANDROID_CMDLINE="console=ttyAMA0 earlycon=pl011,0x10000000"
ANDROID_CMDLINE="${ANDROID_CMDLINE} androidboot.hardware=siliconv"
ANDROID_CMDLINE="${ANDROID_CMDLINE} androidboot.selinux=permissive"
ANDROID_CMDLINE="${ANDROID_CMDLINE} androidboot.veritymode=disabled"
ANDROID_CMDLINE="${ANDROID_CMDLINE} androidboot.force_normal_boot=1"
ANDROID_CMDLINE="${ANDROID_CMDLINE} androidboot.debuggable=1"
ANDROID_CMDLINE="${ANDROID_CMDLINE} loglevel=7"

# ── Pre-flight checks ───────────────────────────────

echo "=== SiliconV AOSP Boot Test ==="
echo "Mode: ${MODE}"
echo ""

ERRORS=0

if [ ! -f "${KERNEL_IMAGE}" ]; then
    echo "ERROR: Kernel image not found: ${KERNEL_IMAGE}"
    echo "  Build with: ./scripts/build_kernel.sh"
    ERRORS=$((ERRORS + 1))
fi

if [ ! -f "${SYSTEM_IMG}" ]; then
    echo "ERROR: System image not found: ${SYSTEM_IMG}"
    echo "  Build with: ./scripts/build_android_images.sh"
    ERRORS=$((ERRORS + 1))
fi

if [ ! -f "${VENDOR_IMG}" ]; then
    echo "ERROR: Vendor image not found: ${VENDOR_IMG}"
    echo "  Build with: ./scripts/build_android_images.sh"
    ERRORS=$((ERRORS + 1))
fi

if ! command -v "${QEMU_CMD}" &>/dev/null; then
    echo "ERROR: QEMU not found: ${QEMU_CMD}"
    echo "  Install with: apt install qemu-system-arm"
    ERRORS=$((ERRORS + 1))
fi

if [ "${ERRORS}" -gt 0 ]; then
    echo ""
    echo "Pre-flight checks failed (${ERRORS} errors). Fix and retry."
    exit 1
fi

echo "Kernel: ${KERNEL_IMAGE} ($(du -h "${KERNEL_IMAGE}" | cut -f1))"
echo "System: ${SYSTEM_IMG} ($(du -h "${SYSTEM_IMG}" | cut -f1))"
echo "Vendor: ${VENDOR_IMG} ($(du -h "${VENDOR_IMG}" | cut -f1))"
echo ""

# ── QEMU Drive Mapping ──────────────────────────────
# /dev/vda → system.img (read-only, first_stage_mount)
# /dev/vdb → vendor.img (read-only, first_stage_mount)
# /dev/vdc → userdata.img (read-write)
# /dev/vdd → cache.img (read-write)
# /dev/vde → metadata.img (read-write)

QEMU_ARGS=(
    -M virt,gic-version=3
    -cpu cortex-a72
    -smp 4
    -m 4096
    -kernel "${KERNEL_IMAGE}"
    -drive "file=${SYSTEM_IMG},format=raw,if=virtio,readonly=on"
    -drive "file=${VENDOR_IMG},format=raw,if=virtio,readonly=on"
)

# Optional: userdata
if [ -f "${DATA_IMG}" ]; then
    QEMU_ARGS+=(-drive "file=${DATA_IMG},format=raw,if=virtio")
fi

# Optional: cache
if [ -f "${CACHE_IMG}" ]; then
    QEMU_ARGS+=(-drive "file=${CACHE_IMG},format=raw,if=virtio")
fi

# Optional: metadata
if [ -f "${META_IMG}" ]; then
    QEMU_ARGS+=(-drive "file=${META_IMG},format=raw,if=virtio")
fi

# Initramfs (if available)
if [ -f "${INITRAMFS}" ]; then
    QEMU_ARGS+=(-initrd "${INITRAMFS}")
    echo "Initramfs: ${INITRAMFS}"
else
    echo "WARNING: No initramfs found — GSI must provide its own ramdisk"
fi

# Networking
QEMU_ARGS+=(
    -device virtio-net-device,netdev=net0
    -netdev user,id=net0,hostfwd=tcp::5555-:5555
)

# Serial
QEMU_ARGS+=(-nographic)

# Append kernel command line
QEMU_ARGS+=(-append "${ANDROID_CMDLINE}")

# ── Run based on mode ────────────────────────────────

LOG_FILE="${PROJECT_DIR}/build/android_boot.log"
mkdir -p "$(dirname "${LOG_FILE}")"

case "${MODE}" in
    full)
        TIMEOUT=180
        echo "Running QEMU with ${TIMEOUT}s timeout..."
        echo "Log: ${LOG_FILE}"
        echo ""

        timeout "${TIMEOUT}" "${QEMU_CMD}" "${QEMU_ARGS[@]}" 2>&1 | tee "${LOG_FILE}" || true
        ;;

    quick)
        TIMEOUT=60
        echo "Running QEMU with ${TIMEOUT}s timeout (quick mode)..."
        echo ""

        timeout "${TIMEOUT}" "${QEMU_CMD}" "${QEMU_ARGS[@]}" 2>&1 | tee "${LOG_FILE}" || true
        ;;

    debug)
        echo "Starting QEMU in debug mode (interactive)..."
        echo "Use Ctrl-A X to exit QEMU"
        echo ""

        "${QEMU_CMD}" "${QEMU_ARGS[@]}" -serial mon:stdio
        ;;

    *)
        echo "ERROR: Unknown mode: ${MODE}"
        echo "Usage: $0 [full|quick|debug]"
        exit 1
        ;;
esac

# ── Verify boot milestones ──────────────────────────

echo ""
echo "=== Boot Milestone Verification ==="
echo ""

check_milestone() {
    local label="$1"
    local pattern="$2"
    if grep -q "${pattern}" "${LOG_FILE}" 2>/dev/null; then
        echo "  ✓ ${label}"
        return 0
    else
        echo "  ✗ ${label}"
        return 1
    fi
}

MILESTONES=0
TOTAL=0

# Check each milestone
TOTAL=$((TOTAL + 1)); check_milestone "Kernel boots" "Linux version" && MILESTONES=$((MILESTONES + 1)) || true
TOTAL=$((TOTAL + 1)); check_milestone "Android cmdline" "androidboot.hardware=siliconv" && MILESTONES=$((MILESTONES + 1)) || true
TOTAL=$((TOTAL + 1)); check_milestone "Init starts" "init:" && MILESTONES=$((MILESTONES + 1)) || true
TOTAL=$((TOTAL + 1)); check_milestone "FirstStageMount" "Mounted.*system" && MILESTONES=$((MILESTONES + 1)) || true
TOTAL=$((TOTAL + 1)); check_milestone "SELinux loaded" "SELinux.*loaded\|selinux.*permissive" && MILESTONES=$((MILESTONES + 1)) || true
TOTAL=$((TOTAL + 1)); check_milestone "SecondStage init" "Starting.*services\|SecondStage" && MILESTONES=$((MILESTONES + 1)) || true
TOTAL=$((TOTAL + 1)); check_milestone "servicemanager" "servicemanager" && MILESTONES=$((MILESTONES + 1)) || true
TOTAL=$((TOTAL + 1)); check_milestone "logd started" "logd\|logcat" && MILESTONES=$((MILESTONES + 1)) || true
TOTAL=$((TOTAL + 1)); check_milestone "Binder working" "binder.*enabled\|Binder.*driver" && MILESTONES=$((MILESTONES + 1)) || true

echo ""
echo "Milestones: ${MILESTONES}/${TOTAL}"

if [ "${MILESTONES}" -ge 7 ]; then
    echo ""
    echo "🎉 Phase 4 MILESTONE REACHED: Android init is running!"
    exit 0
elif [ "${MILESTONES}" -ge 4 ]; then
    echo ""
    echo "⚠ Partial boot — init is progressing but not all services started"
    exit 0
else
    echo ""
    echo "✗ Boot failed or insufficient progress"
    echo "Try debug mode: ./scripts/test_android_qemu.sh debug"
    exit 1
fi
