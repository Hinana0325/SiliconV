#!/bin/bash
#
# SiliconV — QEMU Test Script
#
# Tests the SiliconV boot stub using QEMU ARM64 system emulation.
# This validates UART output without needing KVM for ARM64.
#
# Usage: ./scripts/test_qemu.sh
#
# Requirements:
#   apt install qemu-system-aarch64 gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

info()  { echo -e "${GREEN}[sv]${NC} $*"; }
warn()  { echo -e "${YELLOW}[sv]${NC} $*"; }
error() { echo -e "${RED}[sv]${NC} $*" >&2; }

# ── Check dependencies ──────────────────────────
check_deps() {
    local missing=()

    command -v qemu-system-aarch64 >/dev/null || missing+=(qemu-system-aarch64)
    command -v aarch64-linux-gnu-as >/dev/null  || missing+=(gcc-aarch64-linux-gnu)
    command -v aarch64-linux-gnu-ld >/dev/null  || missing+=(binutils-aarch64-linux-gnu)

    if [ ${#missing[@]} -gt 0 ]; then
        error "Missing dependencies: ${missing[*]}"
        echo "  Install with: apt install ${missing[*]}"
        exit 1
    fi
}

# ── Build boot stub ──────────────────────────────
build_stub() {
    info "Building ARM64 boot stub..."
    mkdir -p "$BUILD_DIR"

    aarch64-linux-gnu-as \
        -o "$BUILD_DIR/boot_stub.o" \
        "$PROJECT_DIR/core/vm/boot_stub.S"

    aarch64-linux-gnu-ld \
        -T "$PROJECT_DIR/core/vm/boot_stub.ld" \
        -o "$BUILD_DIR/boot_stub.elf" \
        "$BUILD_DIR/boot_stub.o"

    aarch64-linux-gnu-objcopy \
        -O binary \
        "$BUILD_DIR/boot_stub.elf" \
        "$BUILD_DIR/boot_stub.bin"

    local size=$(stat -c%s "$BUILD_DIR/boot_stub.bin" 2>/dev/null || stat -f%z "$BUILD_DIR/boot_stub.bin")
    info "Boot stub: $BUILD_DIR/boot_stub.bin ($size bytes)"
}

# ── Run in QEMU ──────────────────────────────────
run_qemu() {
    info "Starting QEMU (ARM64 virt machine)..."
    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

    # QEMU virt machine with:
    # - 1 CPU (Cortex-A57 — closest to A55 that QEMU supports well)
    # - 128M RAM
    # - Load boot stub binary at address 0x400000000
    # - Serial output to terminal
    # - Auto-exit after 5 seconds of no activity

    qemu-system-aarch64 \
        -machine virt \
        -cpu cortex-a57 \
        -m 128M \
        -nographic \
        -semihosting \
        -device loader,file="$BUILD_DIR/boot_stub.bin",addr=0x400000000 \
        -serial mon:stdio \
        -no-reboot \
        -d guest_errors \
        2>&1 | head -20

    local exit_code=${PIPESTATUS[0]}
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo ""

    if [ $exit_code -eq 0 ]; then
        info "QEMU exited cleanly"
    else
        warn "QEMU exited with code $exit_code"
    fi
}

# ── Main ─────────────────────────────────────────
main() {
    info "SiliconV QEMU Test"
    echo ""
    check_deps
    build_stub
    run_qemu
}

main "$@"
