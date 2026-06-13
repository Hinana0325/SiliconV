#!/bin/bash
# SiliconV — 阿里云镜像一键下载脚本
#
# 从阿里云镜像站下载 SiliconV 所需的所有资源：
#   - Linux 内核源码（mirrors.aliyun.com/linux-kernel）
#   - Busybox 源码（mirrors.aliyun.com/debian）
#   - AOSP GSI 镜像引导（Google 源，附阿里云 AOSP 源码镜像备选）
#
# Usage:
#   ./scripts/download_from_aliyun.sh              # 交互式菜单
#   ./scripts/download_from_aliyun.sh kernel       # 仅下载内核
#   ./scripts/download_from_aliyun.sh rootfs       # 仅构建最小 rootfs
#   ./scripts/download_from_aliyun.sh all          # 下载全部（不含 GSI）
#   ./scripts/download_from_aliyun.sh gsi          # 下载 GSI 引导

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
MODE="${1:-menu}"

# ══════════════════════════════════════════════════
# 阿里云镜像 URL
# ══════════════════════════════════════════════════

ALIYUN_KERNEL_URL="https://mirrors.aliyun.com/linux-kernel/v6.x/linux-6.6.tar.xz"
ALIYUN_BUSYBOX_URL="https://mirrors.aliyun.com/debian/pool/main/b/busybox/busybox_1.37.0.orig.tar.bz2"
ALIYUN_AOSP_URL="https://mirrors.aliyun.com/android.googlesource.com/"

# ══════════════════════════════════════════════════
# 工具函数
# ══════════════════════════════════════════════════

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
BLUE='\033[0;34m'
NC='\033[0m'

info()  { echo -e "  ${GREEN}✓${NC} $1"; }
warn()  { echo -e "  ${YELLOW}⚠${NC} $1"; }
error() { echo -e "  ${RED}✗${NC} $1"; }
title() { echo -e "\n${BLUE}═══ $1 ═══${NC}\n"; }

check_prereqs() {
    local missing=""
    for cmd in curl wget tar; do
        if ! command -v "$cmd" &>/dev/null; then
            missing="$missing $cmd"
        fi
    done
    if [ -n "$missing" ]; then
        error "Missing required tools:$missing"
        echo "  Install: apt install curl wget tar"
        exit 1
    fi
}

# ══════════════════════════════════════════════════
# 各步骤实现
# ══════════════════════════════════════════════════

download_kernel() {
    title "下载 Linux Kernel 6.6 (ARM64)"
    echo "来源: ${ALIYUN_KERNEL_URL}"
    echo ""

    local KERNEL_DIR="${PROJECT_DIR}/kernel/build"

    if [ -d "${KERNEL_DIR}/arch/arm64" ]; then
        info "内核源码已存在: ${KERNEL_DIR}"
        echo "  如需重新下载请: rm -rf ${KERNEL_DIR}"
        return 0
    fi

    mkdir -p "${KERNEL_DIR}"

    echo "正在下载 linux-6.6.tar.xz (约 140MB)..."
    if curl -fSL --connect-timeout 30 --progress-bar \
        -o /tmp/linux-6.6.tar.xz \
        "${ALIYUN_KERNEL_URL}" 2>/dev/null; then
        info "下载完成，正在解压..."

        tar -xJf /tmp/linux-6.6.tar.xz -C "${KERNEL_DIR}" --strip-components=1
        rm -f /tmp/linux-6.6.tar.xz

        info "内核源码已解压到: ${KERNEL_DIR}"
        echo ""
        echo "下一步构建:"
        echo "  cd ${KERNEL_DIR}"
        echo "  make ARCH=arm64 defconfig"
        echo "  # 或使用项目构建脚本:"
        echo "  ${SCRIPT_DIR}/build_kernel.sh"
    else
        error "内核下载失败"
        echo "  请手动下载: wget ${ALIYUN_KERNEL_URL}"
        return 1
    fi
}

download_busybox() {
    title "下载 Busybox 源码 (ARM64 rootfs 所需)"
    echo "来源: ${ALIYUN_BUSYBOX_URL}"
    echo ""

    local BUSYBOX_DIR="${PROJECT_DIR}/build/busybox-src"

    if [ -f "${BUSYBOX_DIR}/Makefile" ]; then
        info "Busybox 源码已存在: ${BUSYBOX_DIR}"
        return 0
    fi

    mkdir -p "${BUSYBOX_DIR}"

    echo "正在下载 busybox_1.37.0.orig.tar.bz2..."
    if curl -fSL --connect-timeout 30 --progress-bar \
        -o /tmp/busybox.tar.bz2 \
        "${ALIYUN_BUSYBOX_URL}" 2>/dev/null; then
        info "下载完成，正在解压..."
        if ! tar -xf /tmp/busybox.tar.bz2 -C "${BUSYBOX_DIR}" --strip-components=1 2>/dev/null; then
            # Fallback: extract to temp then move
            local TMPDIR
            TMPDIR=$(mktemp -d /tmp/busybox-extract-XXXXXX)
            tar -xf /tmp/busybox.tar.bz2 -C "${TMPDIR}" 2>/dev/null
            mv "${TMPDIR}"/busybox-*/* "${BUSYBOX_DIR}/" 2>/dev/null || true
            rm -rf "${TMPDIR}"
        fi
        rm -f /tmp/busybox.tar.bz2
        info "Busybox 源码已解压到: ${BUSYBOX_DIR}"
    else
        error "Busybox 下载失败"
        echo "  请手动下载: wget ${ALIYUN_BUSYBOX_URL}"
        return 1
    fi
}

create_rootfs() {
    title "创建最小 rootfs 镜像"
    echo "需要 root 权限创建 ext4 文件系统"
    echo ""

    if [ -f "${PROJECT_DIR}/build/rootfs.img" ]; then
        info "rootfs.img 已存在: ${PROJECT_DIR}/build/rootfs.img"
        echo "  如需重建请: rm ${PROJECT_DIR}/build/rootfs.img"
        return 0
    fi

    if [ "$(id -u)" -eq 0 ]; then
        "${SCRIPT_DIR}/create_rootfs.sh"
    else
        echo "需要 root 权限，正在使用 sudo..."
        sudo "${SCRIPT_DIR}/create_rootfs.sh"
    fi
}

guide_gsi() {
    title "AOSP GSI 镜像下载引导"
    echo ""
    echo "GSI (Generic System Image) 是 Google 提供的预编译 Android 系统镜像，"
    echo "约 2GB，用于在 SiliconV 虚拟机中启动完整 Android 系统。"
    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "  ⚠️ 注意：阿里云镜像站不提供 GSI 预编译镜像"
    echo "  （阿里云仅镜像 AOSP 源码，非预编译产物）"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo ""
    echo "📱 推荐下载方式："
    echo ""
    echo "  方式1 (推荐): Google 中国开发者站"
    echo "    https://developer.android.google.cn/topic/generic-system-image/releases"
    echo "    → 选择 'ARM64 + Vanilla Full (VF) + userdebug'"
    echo "    → 下载对应 Android 版本 (14 或 15)"
    echo "    → 解压后放入: ${PROJECT_DIR}/build/gsi/"
    echo ""
    echo "  方式2: 使用自动下载脚本 (直连 Google)"
    echo "    ${SCRIPT_DIR}/download_gsi.sh"
    echo ""
    echo "  方式3: 从阿里云 AOSP 镜像编译源码 (耗时较长，约需 200GB 磁盘)"
    echo "    ${ALIYUN_AOSP_URL}"
    echo ""
    echo "  💡 如果仅测试内核启动，无需 GSI："
    echo "    sudo ${SCRIPT_DIR}/create_rootfs.sh"
    echo "    sudo ${SCRIPT_DIR}/create_minimal_rootfs.sh"
    echo ""
}

show_status() {
    title "下载状态总览"

    # Kernel
    if [ -d "${PROJECT_DIR}/kernel/build/arch/arm64" ]; then
        info "内核源码: kernel/build/"
    elif [ -f "${PROJECT_DIR}/kernel/out/Image" ]; then
        info "内核镜像: kernel/out/Image (已编译)"
    else
        warn "内核源码: 未下载"
    fi

    # Busybox
    if [ -f "${PROJECT_DIR}/build/busybox-src/Makefile" ]; then
        info "Busybox 源码: build/busybox-src/"
    else
        warn "Busybox 源码: 未下载"
    fi

    # Rootfs
    if [ -f "${PROJECT_DIR}/build/rootfs.img" ]; then
        local sz
        sz=$(du -h "${PROJECT_DIR}/build/rootfs.img" | cut -f1)
        info "Rootfs 镜像: build/rootfs.img (${sz})"
    else
        warn "Rootfs 镜像: 未创建"
    fi

    # Minimal rootfs
    if [ -f "${PROJECT_DIR}/build/rootfs-minimal.img" ]; then
        local sz
        sz=$(du -h "${PROJECT_DIR}/build/rootfs-minimal.img" | cut -f1)
        info "最小 rootfs: build/rootfs-minimal.img (${sz})"
    fi

    # GSI
    if [ -f "${PROJECT_DIR}/build/gsi/system.img" ]; then
        local sz
        sz=$(du -h "${PROJECT_DIR}/build/gsi/system.img" | cut -f1)
        info "GSI system.img: build/gsi/system.img (${sz})"
    else
        warn "GSI: 未下载 (可选，仅完整 Android 启动需要)"
    fi

    echo ""
}

# ══════════════════════════════════════════════════
# 交互式菜单
# ══════════════════════════════════════════════════

interactive_menu() {
    show_status

    echo "请选择操作："
    echo ""
    echo "  [1] 下载全部 (内核 + busybox + rootfs)      ← 推荐首次使用"
    echo "  [2] 下载内核源码 (Linux 6.6 ARM64, ~140MB)"
    echo "  [3] 下载 busybox 源码 + 创建 rootfs"
    echo "  [4] GSI 镜像下载引导"
    echo "  [5] 显示当前状态"
    echo "  [q] 退出"
    echo ""

    read -r -p "选择 [1-5/q]: " choice

    case "${choice}" in
        1)
            download_kernel || true
            download_busybox || true
            create_rootfs || true
            show_status
            echo ""
            echo "如果内核源码未包含 Android 补丁，使用:"
            echo "  ${SCRIPT_DIR}/build_kernel.sh android14-6.6"
            echo "  (build_kernel.sh 会自动尝试从阿里云 AOSP 镜像获取完整内核)"
            ;;
        2)
            download_kernel || true
            ;;
        3)
            download_busybox || true
            create_rootfs || true
            ;;
        4)
            guide_gsi
            ;;
        5)
            show_status
            ;;
        q|Q)
            echo "退出"
            exit 0
            ;;
        *)
            echo "无效选择"
            ;;
    esac
}

# ══════════════════════════════════════════════════
# Main
# ══════════════════════════════════════════════════

echo ""
echo "╔══════════════════════════════════════════════════╗"
echo "║   SiliconV - 阿里云镜像下载工具                   ║"
echo "║   所有资源优先从 mirrors.aliyun.com 下载          ║"
echo "╚══════════════════════════════════════════════════╝"
echo ""

check_prereqs

case "${MODE}" in
    menu|"")
        interactive_menu
        ;;
    kernel)
        download_kernel
        ;;
    busybox)
        download_busybox
        ;;
    rootfs)
        create_rootfs
        ;;
    gsi)
        guide_gsi
        ;;
    all)
        download_kernel || true
        download_busybox || true
        create_rootfs || true
        show_status
        ;;
    status)
        show_status
        ;;
    *)
        echo "未知模式: ${MODE}"
        echo "用法: $0 [kernel|busybox|rootfs|gsi|all|status|menu]"
        exit 1
        ;;
esac
