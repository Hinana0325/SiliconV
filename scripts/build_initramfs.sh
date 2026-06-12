#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
#
# Build a minimal initramfs cpio archive for kernel boot testing.
#
# Compiles a static ARM64 C program as /init, packs it with empty
# proc/ sys/ dev/ directories into a gzip-compressed cpio archive.
#
# Prerequisites: aarch64-linux-gnu-gcc
#
# Usage:
#   ./scripts/build_initramfs.sh                    # uses project source
#   INIT_C_SRC=/tmp/my_init.c ./scripts/build_initramfs.sh
#   OUTPUT=/tmp/my_initramfs.cpio.gz ./scripts/build_initramfs.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

INIT_C_SRC="${INIT_C_SRC:-${PROJECT_DIR}/tests/integration/test_kernel_boot_qemu.c}"
OUTPUT="${OUTPUT:-${PROJECT_DIR}/build/initramfs.cpio.gz}"
BUILD_DIR="$(mktemp -d)"

echo "Building initramfs..."
echo "  Source: ${INIT_C_SRC}"
echo "  Output: ${OUTPUT}"

# Build static init binary
aarch64-linux-gnu-gcc -static -Os -o "${BUILD_DIR}/init" "${INIT_C_SRC}"

# Pack into cpio archive
mkdir -p "${BUILD_DIR}/proc" "${BUILD_DIR}/sys" "${BUILD_DIR}/dev"
cd "${BUILD_DIR}"
find . | cpio -H newc -o --quiet | gzip > "${OUTPUT}"

SIZE=$(stat -c%s "${OUTPUT}")
echo "Done: ${SIZE} bytes"

# Cleanup
rm -rf "${BUILD_DIR}"
