# SiliconV — Makefile (x86_64 Linux, TCG + KVM backends)
#
# Build:  make
# Clean:  make clean
# Test:   make test

CC      = gcc
CFLAGS  = -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare \
          -D_GNU_SOURCE -DSV_HAS_KVM=1 \
          -I. -Ithird_party/lzfse_stub -Ihypervisor/abstraction
LDFLAGS = -lm

# ── Source Files ──────────────────────────────────

# Core
SRCS += core/irq/gic.c
SRCS += core/memory/dtb.c
SRCS += core/object/psci.c
SRCS += core/vm/machine.c
SRCS += core/vm/machine_apple.c
SRCS += core/vm/bootimg.c

# Devices — Android
SRCS += devices/uart/pl011.c
SRCS += devices/transport/virtio_mmio.c
SRCS += devices/virtio-blk/virtio_blk.c
SRCS += devices/virtio-net/virtio_net.c
SRCS += devices/virtio-net/tap_backend.c
SRCS += devices/virtio-console/virtio_console.c

# Devices — Apple
SRCS += devices/apple-aic/apple_aic.c
SRCS += devices/apple-uart/apple_uart.c
SRCS += devices/apple-dart/apple_dart.c
SRCS += devices/apple-sep/apple_sep.c
SRCS += devices/apple-wdt/apple_wdt.c
SRCS += devices/apple-nvram/apple_nvram.c
SRCS += devices/apple-timer/apple_timer.c
SRCS += devices/apple-gpio/apple_gpio.c
SRCS += devices/apple-i2c/apple_i2c.c
SRCS += devices/apple-spmi/apple_spmi.c
SRCS += devices/apple-nvme/apple_nvme.c

# Boot — Apple
SRCS += core/boot/apple/img4.c
SRCS += core/boot/apple/macho.c
SRCS += core/boot/apple/dtre.c
SRCS += core/boot/apple/bootargs.c
SRCS += core/boot/apple/xnu_boot.c

# Hypervisor
SRCS += hypervisor/abstraction/hv.c
SRCS += hypervisor/tcg/tcg.c
SRCS += hypervisor/tcg/tcg_decode.c
SRCS += hypervisor/tcg/tcg_exec.c
SRCS += hypervisor/tcg/tcg_mmu.c
SRCS += hypervisor/kvm/kvm.c

# Frontend
SRCS += frontend/cli/main.c

OBJS = $(SRCS:.c=.o)
DEPS = $(SRCS:.c=.d)

# ── Targets ───────────────────────────────────────

TARGET = sv-cli

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Build complete: $(TARGET)"

%.o: %.c
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

-include $(DEPS)

clean:
	rm -f $(OBJS) $(DEPS) $(TARGET)
	rm -f build_test_nvme

# ── Tests ─────────────────────────────────────────

TEST_NVME_OBJS = tests/unit/test_apple_nvme.o devices/apple-nvme/apple_nvme.o

test: test_nvme

test_nvme: $(TEST_NVME_OBJS)
	$(CC) $(CFLAGS) -o build_test_nvme $^
	./build_test_nvme

.PHONY: test test_nvme
