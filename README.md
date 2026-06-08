# SiliconV

[English](README.md) | [中文](README.zh-CN.md)

A virtual phone hardware specification and hypervisor platform.

**SiliconV is not an emulator. It defines a virtual hardware interface.**

## What Is This?

SiliconV defines a **virtual ARM64 phone platform** — a consistent hardware abstraction that lets you:

- Boot unmodified Linux/Android kernels
- Run AOSP Android in a VM with near-native performance
- Develop and test without physical hardware
- Transplant ROM images between devices (future)

## Hardware Stack (v0 — Frozen)

```
Host:     KVM (Linux) / HVF (macOS)
Kernel:   Android Common Kernel 6.6
GPU:      VirGL (Mesa + virglrenderer)
Storage:  Virtio-BLK
Network:  Virtio-NET
Display:  virtio-gpu + minigbm + drm_hwcomposer
```

## Architecture

```
┌─────────────────────────────────────────────────┐
│                 Android (AOSP GSI)               │
├──────────┬──────────┬──────────┬────────────────┤
│ SurfaceFlinger │ HWComposer │ gralloc │    EGL/GLES   │
│  (AOSP)       │(drm_hwcomp)│(minigbm)│   (Mesa virgl)│
├──────────┴──────────┴──────────┴────────────────┤
│              Linux Kernel 6.6 (ARM64)            │
│    Binder │ Ashmem(memfd) │ DMABUF │ DRM/KMS    │
├─────────────────────────────────────────────────┤
│              SVABI v0 (hardware contract)         │
│  GICv3 │ PL011 │ Virtio-BLK/NET/GPU/INPUT/CONSOLE│
├─────────────────────────────────────────────────┤
│              SiliconV Hypervisor                  │
│         KVM (Linux) / HVF (macOS)                │
├─────────────────────────────────────────────────┤
│              Host OS / Hardware                   │
└─────────────────────────────────────────────────┘
```

## Project Structure

```
├── spec/              # Hardware specification (frozen)
│   ├── svabi/         #   ABI contract
│   ├── memory/        #   MMIO layout
│   ├── irq/           #   IRQ assignments
│   ├── devices/       #   Virtio device matrix
│   └── boot/          #   Boot flow + DTB schema
│
├── core/              # Platform core
│   ├── vm/            #   Machine + boot stub + bootimg parser
│   ├── irq/           #   GICv3 emulation
│   ├── memory/        #   DTB generator
│   └── object/        #   PSCI (CPU lifecycle)
│
├── hypervisor/        # Hardware virtualization
│   ├── abstraction/   #   Backend interface
│   └── kvm/           #   KVM backend (ARM64)
│
├── devices/           # Device emulation
│   ├── uart/          #   PL011 serial console
│   ├── virtio-blk/    #   Block device
│   └── transport/     #   Virtio MMIO transport
│
├── android/           # Android integration
│   ├── aosp/          #   AOSP guide
│   ├── binder/        #   Binder/Ashmem/DMABUF
│   ├── graphics/      #   Graphics pipeline
│   ├── init/          #   Android init
│   ├── shims/         #   HAL shims
│   └── sepolicy/      #   SELinux policy
│
├── kernel/            # Kernel configuration
│   └── configs/       #   android.config (6.6)
│
├── scripts/           # Build & test scripts
├── frontend/          # CLI launcher
├── docs/              # Documentation
└── .github/           # CI/CD workflows
```

## Quick Start

### Build (Host)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

### Build Android Kernel

```bash
./scripts/build_kernel.sh android14-6.6
```

### Test with QEMU (requires ARM64 cross-compiler)

```bash
./scripts/test_qemu.sh
```

### Run on ARM64 Host

```bash
./build/siliconv -k Image -r rootfs.img
```

## Development Status

| Phase | Status | Description |
|-------|--------|-------------|
| Phase 0 | ✅ | Specification Freeze |
| Phase 1 | ✅ | Hello SiliconV (UART + KVM) |
| Phase 2 | ✅ | Full Linux Boot (GICv3 + PSCI + virtio-blk) |
| Phase 3 | 🔄 | Android Kernel (Binder + DMABUF + init) |
| Phase 4 | — | AOSP Init Boot (logcat) |
| Phase 5 | — | SurfaceFlinger (black screen) |
| Phase 6 | — | First Light (Launcher visible) |

## Specifications

- [SVABI v0](spec/svabi/svabi-v0.md) — ABI contract
- [Hardware Stack](spec/hardware-stack.md) — Frozen hardware choices
- [MMIO Layout](spec/memory/mmio-map.md) — Device address map
- [IRQ Layout](spec/irq/irq-map.md) — Interrupt assignments
- [Boot Flow](spec/boot/boot-flow.md) — Boot sequence
- [DTB Schema](spec/boot/dtb-schema.md) — Device tree format
- [Virtio Matrix](spec/devices/virtio-matrix.md) — Required devices

## Contributing

SiliconV follows these principles:

1. **AOSP first** — never touch vendor ROMs until AOSP is rock solid
2. **Reuse, don't rewrite** — minigbm, drm_hwcomposer, Mesa, virglrenderer
3. **Spec before code** — every change starts with a spec update
4. **VirGL before Venus** — 3D via VirGL first, Vulkan later

## License

TBD
