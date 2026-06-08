# SiliconV — Docker Build Environment
#
# Provides a reproducible build environment with all required tools.
# Includes ARM64 cross-compiler, QEMU, and AOSP build tools.
#
# Usage:
#   docker build -t siliconv-build docker/
#   docker run -v $(pwd):/workspace -it siliconv-build

FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
ENV CROSS_COMPILE=aarch64-linux-gnu-

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    gcc-aarch64-linux-gnu \
    g++-aarch64-linux-gnu \
    binutils-aarch64-linux-gnu \
    qemu-system-arm \
    qemu-efi-aarch64 \
    git \
    wget \
    curl \
    unzip \
    python3 \
    python3-pip \
    device-tree-compiler \
    dosfstools \
    e2fsprogs \
    mtools \
    simg2img \
    android-sdk-libsparse-utils \
    libssl-dev \
    libncurses-dev \
    flex \
    bison \
    bc \
    kmod \
    cpio \
    rsync \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

# Default command: show help
CMD ["echo", "SiliconV Build Environment. Mount your project to /workspace."]
