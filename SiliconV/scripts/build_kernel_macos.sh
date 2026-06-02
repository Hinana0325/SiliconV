#!/bin/bash
#
# SiliconV — Android Kernel Build Script (macOS / LLVM)
#
# Builds an ARM64 Linux kernel with Android patches for SiliconV.
# Uses LLVM/clang instead of aarch64-linux-gnu-gcc.
#
# Usage: ./scripts/build_kernel_macos.sh [branch]
#   branch: linux-6.6.y (default), linux-6.1.y, master
#
# Requirements:
#   brew install llvm lld

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build"
KERNEL_DIR="$BUILD_DIR/kernel"
BRANCH="${1:-linux-6.6.y}"
KERNEL_URL="${2:-https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable}"

export PATH="/usr/local/opt/llvm/bin:/usr/local/opt/lld/bin:/usr/local/opt/make/libexec/gnubin:/usr/local/opt/gnu-sed/libexec/gnubin:$PATH"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

info()  { echo -e "${GREEN}[sv]${NC} $*"; }
warn()  { echo -e "${YELLOW}[sv]${NC} $*"; }
error() { echo -e "${RED}[sv]${NC} $*" >&2; }

NPROC=$(sysctl -n hw.logicalcpu 2>/dev/null || echo 4)

check_deps() {
    local missing=()
    command -v clang >/dev/null || missing+=(clang)
    command -v ld.lld >/dev/null || missing+=(ld.lld)
    command -v bc >/dev/null || missing+=(bc)
    command -v bison >/dev/null || missing+=(bison)
    command -v flex >/dev/null || missing+=(flex)

    if [ ${#missing[@]} -gt 0 ]; then
        error "Missing: ${missing[*]}"
        exit 1
    fi
    info "Using: clang at $(which clang), ld.lld at $(which ld.lld)"
    info "Kernel source: $KERNEL_URL ($BRANCH)"
}

clone_kernel() {
    if [ -d "$KERNEL_DIR/.git" ]; then
        info "Kernel already cloned at $KERNEL_DIR"
        return
    fi

    info "Cloning AOSP common kernel ($BRANCH)..."
    mkdir -p "$BUILD_DIR"
    git clone --depth=1 \
        -b "$BRANCH" \
        "$KERNEL_URL" \
        "$KERNEL_DIR"
}

apply_config() {
    info "Applying SiliconV Android config..."

    cd "$KERNEL_DIR"

    # Start with arm64 defconfig
    make LLVM=1 ARCH=arm64 defconfig

    # Merge Android config
    if [ -f "$PROJECT_DIR/kernel/configs/android.config" ]; then
        scripts/kconfig/merge_config.sh \
            -m .config \
            "$PROJECT_DIR/kernel/configs/android.config"
        make LLVM=1 ARCH=arm64 olddefconfig
    fi

    info "Config applied"
}

build_kernel() {
    info "Building kernel with $NPROC parallel jobs..."

    cd "$KERNEL_DIR"

    make LLVM=1 ARCH=arm64 -j"$NPROC"

    local image="$KERNEL_DIR/arch/arm64/boot/Image"
    if [ -f "$image" ]; then
        local size=$(stat -f%z "$image" 2>/dev/null || stat -c%s "$image" 2>/dev/null)
        info "Kernel built: $image ($(( size / 1024 / 1024 )) MB)"
        cp "$image" "$BUILD_DIR/Image"
    else
        error "Kernel build failed -- no Image produced"
        exit 1
    fi
}

build_modules() {
    info "Building modules..."

    cd "$KERNEL_DIR"
    make LLVM=1 ARCH=arm64 modules -j"$NPROC"

    make LLVM=1 ARCH=arm64 \
        INSTALL_MOD_PATH="$BUILD_DIR/modules" \
        modules_install

    info "Modules installed to $BUILD_DIR/modules"
}

main() {
    info "SiliconV Android Kernel Builder (macOS/LLVM)"
    info "  Branch: $BRANCH"
    info ""

    check_deps
    clone_kernel
    apply_config
    build_kernel
    build_modules

    echo ""
    info "Build complete!"
    info "  Image: $BUILD_DIR/Image"
    info "  Modules: $BUILD_DIR/modules/"
    echo ""
    info "Test with:"
    info "  ./scripts/test_qemu.sh"
}

main "$@"
