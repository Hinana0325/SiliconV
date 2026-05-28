# SiliconV Roadmap

## Phase 0: Specification Freeze ✅ (Current)

**Goal:** Define the virtual hardware. Freeze the ABI.

- [x] MMIO Layout (`spec/memory/mmio-map.md`)
- [x] IRQ Layout (`spec/irq/irq-map.md`)
- [x] Boot Flow (`spec/boot/boot-flow.md`)
- [x] DTB Schema (`spec/boot/dtb-schema.md`)
- [x] Virtio Matrix (`spec/devices/virtio-matrix.md`)
- [x] SVABI v0 (`spec/svabi/svabi-v0.md`)
- [ ] GPU specification (`spec/gpu/`)

## Phase 1: Hello SiliconV

**Goal:** Print "Hello SiliconV" from a guest VM.

- [x] KVM hypervisor backend (Linux)
- [x] Minimal VM creation (ARM64, 1 CPU, 256M RAM)
- [x] PL011 UART emulation
- [x] GICv3 emulation (minimal)
- [x] Guest physical memory setup
- [x] Direct kernel boot
- [x] DTB generator
- [x] UART console output: `Hello SiliconV`

**Milestone:** ✅ Guest kernel boots, prints to UART. (2026-05-29)

## Phase 2: Linux Boot

**Goal:** Full Linux boot log from guest.

- [x] Complete GICv3 (SGI, PPI, SPI)
- [x] ARM generic timer
- [x] Multi-CPU support (4 cores)
- [x] 4G guest RAM
- [x] PSCI implementation
- [x] virtio-blk (root filesystem)
- [x] virtio-net (networking)
- [x] Full DTB generation
- [ ] Linux kernel boots to shell

**Milestone:** 🔄 In progress (2026-05-29)

## Phase 3: Android Foundation

**Goal:** Boot Android init.

- [ ] Android boot image parser
- [ ] virtio-input (touchscreen + keyboard)
- [ ] virtio-gpu (2D framebuffer)
- [ ] virtio-console (guest ↔ host channel)
- [ ] virtio-fs (shared filesystem)
- [ ] virtio-rng (entropy)
- [ ] RTC and watchdog
- [ ] Android init runs
- [ ] Basic SurfaceFlinger (framebuffer)

**Milestone:** Android boots to launcher.

## Phase 4: ROM Transplant

**Goal:** Run a real Android vendor image.

- [ ] Toolchain: unpack/repack boot images
- [ ] Toolchain: DTB extraction and patching
- [ ] Toolchain: kernel replacement
- [ ] Toolchain: vendor partition patching
- [ ] Shim generator for HAL mismatches
- [ ] SELinux policy adaptation
- [ ] HyperOS / MIUI boot test

**Milestone:** A real ROM boots on SiliconV.

## Phase 5: GPU & Performance

**Goal:** Usable graphics and performance.

- [ ] GPU specification finalization
- [ ] virgl/venus integration (3D)
- [ ] Vulkan passthrough
- [ ] Shader compilation
- [ ] DMA optimization
- [ ] Virtio multiqueue
- [ ] Performance benchmarks

**Milestone:** Android UI is smooth, apps run.

## Phase 6: Multi-Platform

**Goal:** Run on macOS and Windows.

- [ ] HVF backend (macOS)
- [ ] WHPX backend (Windows)
- [ ] Hypervisor abstraction layer
- [ ] Cross-platform launcher
- [ ] Qt frontend
- [ ] Web frontend

**Milestone:** SiliconV runs on all three platforms.

## Phase 7: Ecosystem

**Goal:** Community and tooling.

- [ ] Plugin system for custom devices
- [ ] ROM compatibility database
- [ ] Performance profiling tools
- [ ] CI/CD integration
- [ ] Documentation site
- [ ] Community contributions

---

## Development Principles

1. **Spec before code** — every change starts with a spec update
2. **UART first** — if you can't see output, you can't debug
3. **Linux before Android** — get the kernel right, then layer Android
4. **One device at a time** — don't add a new device until the current one works
5. **Test on real hardware** — benchmark against actual phones
