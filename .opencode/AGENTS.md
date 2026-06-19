# AGENTS.md — SiliconV

ARM64 virtual phone hypervisor platform written in **C11** (no C++ by default; C++17
only for the optional Qt6 GUI). Runs unmodified Linux/Android kernels via KVM (Linux
ARM64), HVF (macOS Apple Silicon), or TCG (pure-software ARM64 emulation on any host).
This is a hypervisor — not an emulator.

## Build & Run

```bash
# Host build (always works on x86_64, ARM64, macOS)
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# Dry-run validation (works on any host, no ARM64 needed)
./build/sv-cli --dry-run -k Image -r rootfs.img -m 1024 -n 2

# Run on ARM64 host with KVM
./build/sv-cli -k Image -r rootfs.img -m 4096 -n 4
```

**Three CMake frontends:** `sv-cli` (CLI, always), `SiliconV.app` (Cocoa macOS
bundle, `APPLE` only), `siliconv-qt` (Qt6 cross-platform GUI, `-DSV_BUILD_QT=ON`).

The old target name `siliconv` was renamed to `sv-cli` to fix a case-insensitive
macOS collision with the `SiliconV.app` bundle directory. CI artifacts still use
`siliconv` in artifact names; the actual binary is always `build/sv-cli`.

## Test

```bash
# Unit tests (3 exist: DTB, UART, virtio-mmio)
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DSV_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -L unit          # unit tests only
```

No test framework — standalone `.c` files with a manual `CHECK()` macro.

### Integration test scripts (QEMU required)

```bash
# Kernel boot verification (initramfs mode, auto poweroff)
./scripts/test_kernel_qemu.sh
# Same with virtio-blk rootfs
MODE=rootfs ./scripts/test_kernel_qemu.sh

# Full AOSP GSI boot test
./scripts/test_android_qemu.sh           # full mode
./scripts/test_android_qemu.sh quick     # 60s timeout
```

## Cross-compile Dependencies

| What | Package | Needed for |
|------|---------|------------|
| ARM64 assembler/linker | `gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu` | `boot_stub.S` boot stub |
| ARM64 cross-compiler | `gcc-aarch64-linux-gnu` | `build_kernel.sh`, initramfs, tests |
| QEMU ARM64 | `qemu-system-arm` | `test_qemu.sh`, `test_kernel_qemu.sh` |
| `simg2img` | `android-sdk-libsparse-utils` | GSI sparse→raw conversion |

CMake auto-detects the cross-compiler. Without it, `boot_stub.bin` is skipped with a
status message (not a hard error). The boot stub is assembled from `core/vm/boot_stub.S`,
linked via `core/vm/boot_stub.ld`, and objcopy'd to flat binary.

## Project Layout

```
spec/            ← Frozen hardware spec (SVABI v0 — change spec/ BEFORE code)
core/vm/         ← Main loop (machine.c), boot stub, bootimg parser ← ENTRYPOINT
  machine.c      ← sv_machine_init() → sv_machine_run() ties everything together
  boot_stub.S    ← ARM64 assembly: "Hello SiliconV" via PL011 UART
  bootimg.c      ← Android boot.img parser (v0–v4, auto-detect)
core/irq/        ← GICv3 emulation (SGI, PPI, SPI)
core/object/     ← PSCI (CPU lifecycle)
core/memory/     ← DTB generator (runtime, auto-generated if no DTB provided)
devices/         ← PL011 UART, virtio-blk, virtio-net, virtio-console, virtio MMIO transport
hypervisor/      ← Backend abstraction + KVM + HVF + TCG (+ WHPX stub)
  abstraction/   ← Backend registry: auto-selects best available
  kvm/           ← /dev/kvm API, registered at library init via constructor
  hvf/           ← Hypervisor.framework (Apple Silicon)
  tcg/           ← Pure-software ARM64 emulation (always built, --whole-archive)
  whpx/          ← Stub only; not yet implemented
initramfs/       ← ARM64 first-stage init + reboot-intercept debugging tools
  init.c         ← Full Android GSI boot: mounts, overlays, chroot → AOSP init
  ptrace_wrapper.c  ← ptrace-based reboot() interceptor (debugging)
  reboot_wrapper.c  ← LD_PRELOAD reboot() interceptor (debugging)
  shim_reboot.c     ← .so shim for reboot() interception
frontend/cli/    ← CLI launcher (main.c, getopt parsing)
frontend/cocoa/  ← macOS Cocoa app (ObjC, SiliconV.app bundle)
frontend/qt/     ← Cross-platform Qt6 GUI (Linux + Windows, -DSV_BUILD_QT=ON)
scripts/         ← Build, test, and image creation scripts
  build_all.sh            ← Master: hypervisor + kernel + rootfs
  build_kernel.sh         ← Clones AOSP common kernel, builds Image
  build_initramfs.sh      ← Compiles initramfs/init.c → cpio.gz archive
  build_android_images.sh ← system, vendor, userdata, cache, metadata .img
  test_qemu.sh            ← Quick QEMU boot test
  test_kernel_qemu.sh     ← Kernel boot verification with device checks
  test_android_qemu.sh    ← Full AOSP GSI boot test
kernel/configs/  ← android.config fragment (merge with GKI defconfig)
android/         ← AOSP integration (binder, graphics, init, shims, sepolicy)
tests/           ← unit/ (3 C files), integration/ (real tests, not stubs)
  unit/            ← test_dtb.c, test_uart.c, test_virtio_mmio.c
  integration/     ← test_kernel_boot_qemu.c, test_android_first_stage.c,
                     android_initramfs_init.c
```

## Architecture

**Entrypoint:** `frontend/cli/main.c` → `sv_machine_init()` →
`sv_machine_load_kernel()` → `sv_machine_generate_dtb()` / `sv_machine_load_dtb()`
→ `sv_machine_run()`.

The machine API (`core/vm/machine.h`) ties together GICv3, PL011, all virtio devices,
PSCI, and DTB generation. MMIO dispatch is `sv_mmio_read`/`sv_mmio_write` in
`machine.c` — KVM backend calls these directly from `KVM_EXIT_MMIO` handling.

**MMIO map (from spec):**
| Address Range | Device |
|--------------|--------|
| `0x08000000 – 0x0800FFFF` | GICv3 Distributor |
| `0x08010000 + cpu*0x20000` | GICv3 Redistributor (per-CPU) |
| `0x10000000 – 0x1000FFFF` | PL011 UART |
| `0x20000000 – 0x2000FFFF` | virtio-blk |
| `0x20010000 – 0x2001FFFF` | virtio-net |
| `0x20040000 – 0x2004FFFF` | virtio-console |

**Hypervisor backends** auto-detected at build time:
- ARM64 Linux → KVM (`/dev/kvm`, GICv3 via `KVM_CREATE_DEVICE`)
- Apple Silicon macOS → HVF (`Hypervisor.framework`)
- Always built → TCG (pure software ARM64 emulation; linked with
  `--whole-archive` because its constructor has no direct symbol refs)
- Everything else → placeholder backend (prints config, no VM execution)

Backend registration uses `__attribute__((constructor))` — not `main()`.

**Boot:** Kernel loaded at `ram_base + 32MB` (0x402000000). DTB at `ram_base + 2MB`
(0x400200000). vCPU init: x0 = DTB addr, x1 = CPU ID, PC = kernel_entry,
CPSR = EL1h with masked DAIF. Supports raw kernel binaries and Android boot.img
(v0–v4, auto-detected by header magic).

## Development Status (from ROADMAP.md)

| Phase | Status | Description |
|-------|--------|-------------|
| 0–2 | ✅ Done | Spec freeze, UART+KVM, full Linux boot (GICv3+PSCI+virtio-blk) |
| 3 | ✅ Done | Android kernel (`/dev/binder`, DMABUF, userfaultfd, virtio) |
| 4 | 🔄 In Progress | AOSP init boot (initramfs, chroot, selinux_setup handoff) |
| 5–6 | — | SurfaceFlinger, first light |

### Phase 4 Setup (GSI Required)

GSI (Generic System Image, ~2GB) is needed for AOSP boot. Automatic download may fail;
use one of these methods:

```bash
# Method 1: Manual download (recommended if auto fails)
# Browse: https://developer.android.google.cn/topic/generic-system-image/releases
# Download "ARM64+VF userdebug" for Android 14 or 15
# Place system.img + boot.img in:
mkdir -p build/gsi
# → copy system.img, boot.img into build/gsi/

# Method 2: Automatic download (Google CI)
./scripts/download_gsi.sh userdebug 14

# Method 3: Skip GSI — build minimal rootfs only
sudo ./scripts/create_rootfs.sh

# After GSI is in place, build all images:
./scripts/build_android_images.sh           # needs build/gsi/system.img
./scripts/build_android_images.sh --skip-system  # vendor/userdata/cache/metadata only

# Full AOSP boot test (GSI required):
./scripts/test_android_qemu.sh              # full mode
./scripts/test_android_qemu.sh quick        # 60s timeout
```

Key principle: **AOSP GSI first, vendor ROMs never until SiliconV is stable.**

## CI (GitHub Actions)

| Workflow | Trigger | Notes |
|----------|---------|-------|
| `ci.yml` | Push/PR to main/master | Build x86_64 host, cross-compile boot stub, full ARM64 cross-build, Qt6 GUI build, spec validation, clang-tidy (optional) |
| `kernel.yml` | Manual dispatch or push to `kernel/configs/**` | Clones AOSP common kernel 6.6, merges `android.config`, builds Image + modules |
| `release.yml` | Tag push `v*` | Builds x86_64 + ARM64 Linux tarballs, creates GitHub release |

**CI runs only on Linux** — no macOS runner. HVF/TCG backends tested manually.

## Apple Profile Device Summary

| Device | File | Status | Notes |
|--------|------|--------|-------|
| AIC | `devices/apple-aic/` | ✅ Done | Interrupt controller (replaces GICv3) |
| S5L UART | `devices/apple-uart/` | ✅ Done | Serial console (replaces PL011) |
| DART×2 | `devices/apple-dart/` | ✅ Done | IOMMU, 2 instances |
| SEP | `devices/apple-sep/` | ✅ Done | Secure Enclave |
| WDT | `devices/apple-wdt/` | ✅ Done | Watchdog timer |
| NVRAM | `devices/apple-nvram/` | ✅ Done | Persistent storage |
| Timer | `devices/apple-timer/` | ✅ Done | 64-bit counter + 4 comparators |
| GPIO | `devices/apple-gpio/` | ✅ Done | 256-pin controller |
| I2C | `devices/apple-i2c/` | ✅ Done | Bus controller |
| SPMI | `devices/apple-spmi/` | ✅ Done | Power management (simulated PMIC) |
| Virtio-GPU | `devices/virtio-gpu/` | ⬜ Stub | Not yet implemented |
| WHPX backend | `hypervisor/whpx/` | ⬜ Stub | Not yet implemented |

## Conventions & Gotchas

- **Spec before code** — any change touching hardware interface updates `spec/` first
- **AOSP first** — vendor ROMs out of scope until AOSP is stable
- **Reuse, don't rewrite** — minigbm, drm_hwcomposer, Mesa, virglrenderer
- **VirGL before Venus** — 3D via VirGL first, Vulkan later
- **C11 strict** (`-std=c11 -Wall -Wextra -Wpedantic`); C++17 only for Qt6
- **Static libs only** — no shared libraries in the build
- **UART is your lifeline** — guest console output is the primary debug channel
- **`--dry-run`** validates config without vCPUs — essential on x86_64 dev machines
  and CI hosts without ARM64 KVM/HVF
- **GIC is built as a separate `sv_gic` static lib** — device tests link it directly
- **Qt6 target** requires `-DSV_BUILD_QT=ON` and `qt6-base-dev`; uses AUTOMOC/AUTORCC/AUTOUIC
- **Android boot.img parser** auto-detects v0–v4 via magic; `bootimg.c` in `core/vm/`
- **Initramfs init** (`initramfs/init.c`) is a static ARM64 binary built by
  `scripts/build_initramfs.sh` — it mounts partitions, sets up tmpfs overlays,
  creates SELinux policy artifacts, and chroot+exec's `/system/bin/init selinux_setup`
- **`file_contexts` at repo root** is a placeholder for secilc (SELinux CIL compiler);
  it must exist as an empty writable file before secilc runs

## OpenCode Commands (from opencode.yml)

- `opencode build` — cmake build pipeline
- `opencode spec-review` — cross-check spec consistency (architect, read-only)
- `opencode kernel-check` — review `kernel/configs/android.config` (kernel agent)
