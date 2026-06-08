# SiliconV Roadmap

> **Strategy: AOSP GSI first. Vendor ROMs never until SiliconV is stable.**
> **Reference: Cuttlefish architecture. Reuse AOSP components.**

---

## Phase 0: Specification Freeze ✅

**Goal:** Define the virtual hardware. Freeze the ABI.

- [x] MMIO Layout (`spec/memory/mmio-map.md`)
- [x] IRQ Layout (`spec/irq/irq-map.md`)
- [x] Boot Flow (`spec/boot/boot-flow.md`)
- [x] DTB Schema (`spec/boot/dtb-schema.md`)
- [x] Virtio Matrix (`spec/devices/virtio-matrix.md`)
- [x] SVABI v0 (`spec/svabi/svabi-v0.md`)

## Phase 1: Hello SiliconV ✅

**Goal:** Print "Hello SiliconV" from a guest VM.

- [x] KVM hypervisor backend (Linux ARM64)
- [x] PL011 UART emulation
- [x] GICv3 emulation (minimal)
- [x] Guest physical memory
- [x] Direct kernel boot
- [x] UART console output

**Milestone:** ✅ Guest kernel boots, prints to UART. (2026-05-29)

## Phase 2: Full Linux Boot ✅

**Goal:** Linux ARM64 boots to shell.

- [x] Complete GICv3 (SGI, PPI, SPI)
- [x] ARM generic timer
- [x] Multi-CPU (4 cores, PSCI)
- [x] 4G guest RAM
- [x] virtio-blk (root filesystem)
- [x] DTB generator (runtime)
- [x] virtio-mmio transport layer

**Milestone:** ✅ Core modules written. (2026-05-29)

## Phase 3: Android Kernel 🔄 (Current)

**Goal:** Linux kernel with Android patches boots.

- [x] Binder driver config + protocol definitions
- [x] Ashmem interface definitions
- [x] ION / DMABUF heap configuration
- [x] Kernel config (android.config)
- [x] SELinux policy (siliconv.te)
- [x] Android boot image parser (v0-v4)
- [x] VM Machine main loop (MMIO dispatch)
- [x] Kernel build script (AOSP common kernel)
- [ ] Build Android kernel from AOSP common kernel
- [ ] Kernel boots with android.config applied
- [ ] Binder, ashmem, ION devices appear in /dev

**Milestone:** Android kernel boots, `/dev/binder` exists.

## Phase 4: AOSP Init Boot

**Goal:** Android init runs, logcat works.

- [ ] virtio-net (networking)
- [ ] virtio-console (guest ↔ host)
- [ ] Android boot image parser
- [ ] fstab.siliconv
- [ ] init.siliconv.rc
- [ ] First stage init completes
- [ ] Second stage init starts
- [ ] servicemanager starts
- [ ] logd starts → `logcat` works
- [ ] zygote starts
- [ ] system_server starts

**Milestone:** `logcat` shows Android system output.

## Phase 5: SurfaceFlinger (Black Screen)

**Goal:** SurfaceFlinger starts, no visible output yet.

- [ ] virtio-gpu (2D framebuffer mode)
- [ ] DRM/KMS kernel driver
- [ ] minigbm gralloc (from AOSP)
- [ ] drm_hwcomposer (from AOSP)
- [ ] SurfaceFlinger starts
- [ ] HWComposer presents frames

**Milestone:** SurfaceFlinger running, screen is black but SF logcat active.

## Phase 6: First Light (Launcher Visible)

**Goal:** Android launcher renders on screen.

- [ ] VirGL integration (virglrenderer)
- [ ] virtio-gpu 3D mode
- [ ] Mesa OpenGL ES driver
- [ ] GPU rendering works
- [ ] Launcher renders
- [ ] Basic touch input (virtio-input)

**Milestone:** Android launcher visible and interactive.

## Phase 7: Input & Interaction

**Goal:** Full touch/keyboard input, apps work.

- [ ] virtio-input (touchscreen)
- [ ] virtio-input (keyboard)
- [ ] virtio-input (mouse/trackpad)
- [ ] Input events reach Android
- [ ] Apps launch and respond to touch
- [ ] virtio-console (adb over virtio)

**Milestone:** Can tap icons, launch apps, type text.

## Phase 8: Performance & Polish

**Goal:** Smooth, usable Android experience.

- [ ] Virtio multiqueue
- [ ] DMA optimization
- [ ] virtio-fs (shared filesystem)
- [ ] virtio-rng (entropy)
- [ ] Audio stub (no crash)
- [ ] Sensor stubs (no crash)
- [ ] Battery simulation
- [ ] Performance benchmarks

**Milestone:** 60fps launcher, apps don't crash.

## Phase 9: Multi-Platform

**Goal:** Run on macOS and Windows.

- [ ] HVF backend (macOS)
- [ ] WHPX backend (Windows)
- [ ] Hypervisor abstraction completed
- [ ] Qt frontend
- [ ] Web frontend

**Milestone:** SiliconV runs on Linux, macOS, Windows.

## Phase 10: Vendor ROMs (Optional)

**Goal:** Only after AOSP works perfectly.

- [ ] Toolchain: boot image unpack/repack
- [ ] Toolchain: DTB extraction/patching
- [ ] Toolchain: kernel replacement
- [ ] Toolchain: vendor partition patching
- [ ] Shim generator for HAL mismatches
- [ ] HyperOS boot test
- [ ] Other vendor ROMs

**Milestone:** A vendor ROM boots on SiliconV.

---

## Development Principles

1. **AOSP first** — never touch vendor ROMs until AOSP is rock solid
2. **Reuse, don't rewrite** — minigbm, drm_hwcomposer, Mesa, virglrenderer
3. **Cuttlefish-compatible** — match their hardware layout where possible
4. **VirGL before Venus** — 3D via VirGL first, Vulkan later
5. **Graphics pipeline is the real challenge** — focus there after kernel boots
6. **UART is your lifeline** — if you can't see logcat, you can't debug
7. **One milestone at a time** — don't skip ahead

## What We're NOT Doing

- ❌ Writing our own gralloc (use minigbm)
- ❌ Writing our own HWComposer (use drm_hwcomposer)
- ❌ Writing our own EGL/GLES driver (use Mesa)
- ❌ Touching HyperOS/OneUI/ColorOS (until Phase 10)
- ❌ Implementing Venus/Vulkan before VirGL works
- ❌ Building a GUI before the pipeline works headless
