#!/bin/bash
#
# SiliconV — Android Kernel Build Script
#
# Builds an ARM64 Linux kernel with Android patches for SiliconV.
# Uses AOSP common kernel (android-mainline or android14-6.1).
#
# Usage: ./scripts/build_kernel.sh [branch]
#   branch: android14-6.6 (default), android14-6.1, android-mainline
#
# Requirements:
#   apt install gcc-aarch64-linux-gnu bc bison flex libssl-dev

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build"
KERNEL_DIR="$BUILD_DIR/kernel"
BRANCH="${1:-android14-6.6}"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

info()  { echo -e "${GREEN}[sv]${NC} $*"; }
warn()  { echo -e "${YELLOW}[sv]${NC} $*"; }
error() { echo -e "${RED}[sv]${NC} $*" >&2; }

check_deps() {
    local missing=()
    command -v aarch64-linux-gnu-gcc >/dev/null || missing+=(gcc-aarch64-linux-gnu)
    command -v bc >/dev/null || missing+=(bc)
    command -v bison >/dev/null || missing+=(bison)
    command -v flex >/dev/null || missing+=(flex)

    if [ ${#missing[@]} -gt 0 ]; then
        error "Missing: ${missing[*]}"
        echo "  apt install ${missing[*]}"
        exit 1
    fi
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
        https://android.googlesource.com/kernel/common \
        "$KERNEL_DIR"
}

apply_config() {
    info "Applying SiliconV Android config..."

    cd "$KERNEL_DIR"

    # Start with arm64 defconfig
    make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- defconfig

    # Merge Android config
    if [ -f "$PROJECT_DIR/kernel/configs/android.config" ]; then
        scripts/kconfig/merge_config.sh \
            -m .config \
            "$PROJECT_DIR/kernel/configs/android.config"
        make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- olddefconfig
    fi

    info "Config applied"
}

build_kernel() {
    info "Building kernel..."

    cd "$KERNEL_DIR"

    make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j$(nproc)

    local image="$KERNEL_DIR/arch/arm64/boot/Image"
    if [ -f "$image" ]; then
        local size=$(stat -c%s "$image" 2>/dev/null || stat -f%z "$image")
        info "Kernel built: $image ($(( size / 1024 / 1024 )) MB)"
        cp "$image" "$BUILD_DIR/Image"
    else
        error "Kernel build failed — no Image produced"
        exit 1
    fi
}

build_modules() {
    info "Building modules..."

    cd "$KERNEL_DIR"
    make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- modules -j$(nproc)

    # Install modules to staging directory
    make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
        INSTALL_MOD_PATH="$BUILD_DIR/modules" \
        modules_install

    info "Modules installed to $BUILD_DIR/modules"
}

generate_dtb() {
    info "Generating SiliconV DTB..."

    # Use our DTB generator if cross-compiler is available
    # Otherwise, extract DTB from kernel build
    local dtbs="$KERNEL_DIR/arch/arm64/boot/dts"
    if [ -d "$dtbs" ]; then
        cp "$dtbs"/*.dtb "$BUILD_DIR/" 2>/dev/null || true
        info "DTBs copied from kernel build"
    fi
}

main() {
    info "SiliconV Android Kernel Builder"
    info "  Branch: $BRANCH"
    echo ""

    check_deps
    clone_kernel
    apply_config
    build_kernel
    build_modules
    generate_dtb

    echo ""
    info "Build complete!"
    info "  Image: $BUILD_DIR/Image"
    info "  DTBs:  $BUILD_DIR/*.dtb"
    info "  Modules: $BUILD_DIR/modules/"
    echo ""
    info "Test with:"
    info "  ./scripts/test_qemu.sh"
}

main "$@"
