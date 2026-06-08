#!/bin/bash
#
# SiliconV — Unified Android Kernel Build Script
#
# Cross-platform builder for ARM64 Linux kernel with Android config.
# Supports Linux (GCC cross), macOS (LLVM/clang), and Windows (WSL/Docker).
#
# Usage: ./scripts/build_kernel.sh [branch]
#   branch: android14-6.6 (default), android14-6.1, android-mainline

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build"
KERNEL_DIR="$BUILD_DIR/kernel"
BRANCH="${1:-android14-6.6}"
KERNEL_URL="${2:-https://android.googlesource.com/kernel/common}"
FALLBACK_URL="https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable"

# Map Android branch names to linux-stable branch names
map_branch_linux_stable() {
    case "$1" in
        android14-6.6) echo "linux-6.6.y" ;;
        android14-6.1) echo "linux-6.1.y" ;;
        android14-5.15) echo "linux-5.15.y" ;;
        android-mainline) echo "master" ;;
        *) echo "$1" ;;
    esac
}

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

info()  { echo -e "${GREEN}[sv]${NC} $*"; }
warn()  { echo -e "${YELLOW}[sv]${NC} $*"; }
error() { echo -e "${RED}[sv]${NC} $*" >&2; }

# ── Platform detection ──

detect_platform() {
    case "$(uname -s)" in
        Linux)  echo "linux"  ;;
        Darwin) echo "macos"  ;;
        MINGW*|MSYS*|CYGWIN*)
            echo "windows"
            ;;
        *)      echo "unknown" ;;
    esac
}

PLATFORM=$(detect_platform)
NPROC=1
case "$PLATFORM" in
    linux)   NPROC=$(nproc 2>/dev/null || grep -c ^processor /proc/cpuinfo 2>/dev/null || echo 4) ;;
    macos)   NPROC=$(sysctl -n hw.logicalcpu 2>/dev/null || echo 4) ;;
    windows) NPROC=$NUMBER_OF_PROCESSORS ;;
esac

# ── Tool checks ──

check_native_macos() {
    local missing=()
    command -v clang >/dev/null || missing+=(clang)
    command -v ld.lld >/dev/null || missing+=(ld.lld)
    command -v bc >/dev/null || missing+=(bc)
    command -v bison >/dev/null || missing+=(bison)
    command -v flex >/dev/null || missing+=(flex)
    if [ ${#missing[@]} -gt 0 ]; then
        return 1
    fi
    return 0
}

check_native_linux() {
    local missing=()
    command -v aarch64-linux-gnu-gcc >/dev/null || missing+=(aarch64-linux-gnu-gcc)
    command -v bc >/dev/null || missing+=(bc)
    command -v bison >/dev/null || missing+=(bison)
    command -v flex >/dev/null || missing+=(flex)
    if [ ${#missing[@]} -gt 0 ]; then
        return 1
    fi
    return 0
}

check_native_windows() {
    # Check for WSL GCC cross-compiler first
    if command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then
        return 0
    fi
    # Check for MSYS2 cross-compiler
    if command -v aarch64-linux-gnu-gcc.exe >/dev/null 2>&1; then
        return 0
    fi
    return 1
}

check_docker() {
    if command -v docker >/dev/null 2>&1; then
        docker info >/dev/null 2>&1 && return 0
    fi
    return 1
}

# ── Install missing macOS tools ──

install_macos_tools() {
    local need_install=false
    for cmd in clang ld.lld bc bison flex; do
        if ! command -v "$cmd" >/dev/null 2>&1; then
            need_install=true
            break
        fi
    done
    if $need_install; then
        if ! command -v brew >/dev/null 2>&1; then
            error "Homebrew not found. Cannot install missing tools."
            return 1
        fi
        info "Installing missing tools via Homebrew..."
        local pkgs=()
        command -v clang >/dev/null 2>&1 || pkgs+=(llvm)
        command -v ld.lld >/dev/null 2>&1 || pkgs+=(lld)
        command -v bc >/dev/null 2>&1 || pkgs+=(bc)
        command -v bison >/dev/null 2>&1 || pkgs+=(bison)
        command -v flex >/dev/null 2>&1 || pkgs+=(flex)
        if [ ${#pkgs[@]} -gt 0 ]; then
            brew install "${pkgs[@]}"
        fi
    fi
    info "All macOS build tools available."
}

# ── Clone kernel ──

clone_kernel() {
    if [ -d "$KERNEL_DIR/.git" ]; then
        info "Kernel already cloned at $KERNEL_DIR"
        return
    fi

    mkdir -p "$BUILD_DIR"

    # Try primary URL first
    local url="$KERNEL_URL"
    local branch="$BRANCH"

    if git clone --depth=1 -b "$branch" "$url" "$KERNEL_DIR" 2>/dev/null; then
        info "Cloned from $url ($branch)"
        return
    fi

    # Fall back to linux-stable
    info "Primary URL failed, trying linux-stable fallback..."
    branch=$(map_branch_linux_stable "$BRANCH")
    rm -rf "$KERNEL_DIR"
    git clone --depth=1 -b "$branch" "$FALLBACK_URL" "$KERNEL_DIR"
    info "Cloned from $FALLBACK_URL ($branch)"
}

# ── Apply config ──

apply_config() {
    info "Applying SiliconV Android config..."

    cd "$KERNEL_DIR"

    if [ "$PLATFORM" = "macos" ]; then
        export PATH="/usr/local/opt/gnu-sed/libexec/gnubin:/usr/local/opt/make/libexec/gnubin:/usr/local/opt/llvm/bin:/usr/local/opt/lld/bin:$PATH"
        make LLVM=1 ARCH=arm64 defconfig
        if [ -f "$PROJECT_DIR/kernel/configs/android.config" ]; then
            scripts/kconfig/merge_config.sh \
                -m .config \
                "$PROJECT_DIR/kernel/configs/android.config"
            make LLVM=1 ARCH=arm64 olddefconfig
        fi
    else
        make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- defconfig
        if [ -f "$PROJECT_DIR/kernel/configs/android.config" ]; then
            scripts/kconfig/merge_config.sh \
                -m .config \
                "$PROJECT_DIR/kernel/configs/android.config"
            make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- olddefconfig
        fi
    fi

    info "Config applied"
}

# ── Build kernel ──

build_kernel() {
    info "Building kernel with $NPROC parallel jobs..."

    cd "$KERNEL_DIR"

    if [ "$PLATFORM" = "macos" ]; then
        export PATH="/usr/local/opt/gnu-sed/libexec/gnubin:/usr/local/opt/make/libexec/gnubin:/usr/local/opt/llvm/bin:/usr/local/opt/lld/bin:$PATH"
        make LLVM=1 ARCH=arm64 -j"$NPROC"
    else
        make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j"$NPROC"
    fi

    local image="$KERNEL_DIR/arch/arm64/boot/Image"
    if [ -f "$image" ]; then
        local size=$(stat -f%z "$image" 2>/dev/null || stat -c%s "$image" 2>/dev/null)
        info "Kernel built: $image ($(( size / 1024 / 1024 )) MB)"
        cp "$image" "$BUILD_DIR/Image"
    else
        error "Kernel build failed — no Image produced"
        exit 1
    fi
}

# ── Build modules ──

build_modules() {
    info "Building modules..."

    cd "$KERNEL_DIR"

    if [ "$PLATFORM" = "macos" ]; then
        export PATH="/usr/local/opt/gnu-sed/libexec/gnubin:/usr/local/opt/make/libexec/gnubin:/usr/local/opt/llvm/bin:/usr/local/opt/lld/bin:$PATH"
        make LLVM=1 ARCH=arm64 modules -j"$NPROC"
        make LLVM=1 ARCH=arm64 \
            INSTALL_MOD_PATH="$BUILD_DIR/modules" \
            modules_install
    else
        make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- modules -j"$NPROC"
        make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
            INSTALL_MOD_PATH="$BUILD_DIR/modules" \
            modules_install
    fi

    info "Modules installed to $BUILD_DIR/modules"
}

# ── Docker build ──

build_via_docker() {
    info "Building via Docker..."

    local dockerfile="$PROJECT_DIR/docker/Dockerfile.kernel"
    local image_tag="siliconv/kernel-builder"

    docker build -t "$image_tag" -f "$dockerfile" "$PROJECT_DIR/docker"

    docker run --rm \
        -v "$PROJECT_DIR:/build/source" \
        -v "$BUILD_DIR:/build/out" \
        "$image_tag" \
        bash -c "
            set -e
            KERNEL_DIR=/build/out/kernel
            SOURCE_DIR=/build/source

            if [ ! -d \"\$KERNEL_DIR/.git\" ]; then
                git clone --depth=1 -b $BRANCH $KERNEL_URL \$KERNEL_DIR || {
                    BRANCH=$(map_branch_linux_stable $BRANCH)
                    rm -rf \$KERNEL_DIR
                    git clone --depth=1 -b \$BRANCH $FALLBACK_URL \$KERNEL_DIR
                }
            fi

            cd \$KERNEL_DIR
            make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- defconfig

            if [ -f \$SOURCE_DIR/kernel/configs/android.config ]; then
                scripts/kconfig/merge_config.sh -m .config \$SOURCE_DIR/kernel/configs/android.config
                make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- olddefconfig
            fi

            make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j\$(nproc)

            cp arch/arm64/boot/Image /build/out/Image

            make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- modules -j\$(nproc)
            make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
                INSTALL_MOD_PATH=/build/out/modules modules_install

            echo '[sv] Docker build complete.'
        "
}

# ── Main ──

main() {
    info "SiliconV Android Kernel Builder"
    info "  Platform: $PLATFORM"
    info "  Branch:   $BRANCH"
    info "  CPUs:     $NPROC"
    echo ""

    # Try native build if possible
    local native_possible=false

    case "$PLATFORM" in
        macos)
            if check_native_macos; then
                native_possible=true
                info "Using native macOS build (LLVM/clang)"
            fi
            ;;
        linux)
            if check_native_linux; then
                native_possible=true
                info "Using native Linux build (GCC cross)"
            fi
            ;;
        windows)
            if check_native_windows; then
                native_possible=true
                info "Using native Windows build"
            fi
            ;;
    esac

    if $native_possible; then
        clone_kernel
        apply_config
        build_kernel
        build_modules
    elif check_docker; then
        info "Native build not available, falling back to Docker..."
        build_via_docker
    else
        error "No native build tools available and Docker is not installed."
        error ""
        case "$PLATFORM" in
            macos)
                error "Install LLVM and LLD: brew install llvm lld"
                ;;
            linux)
                error "Install cross-compiler: apt install gcc-aarch64-linux-gnu bc bison flex libssl-dev"
                ;;
            windows)
                error "Install Docker Desktop or WSL with gcc-aarch64-linux-gnu"
                ;;
        esac
        exit 1
    fi

    echo ""
    info "Build complete!"
    info "  Image:   $BUILD_DIR/Image"
    info "  Modules: $BUILD_DIR/modules/"
    echo ""
    info "Test with:"
    info "  ./scripts/test_qemu.sh"
}

main "$@"
