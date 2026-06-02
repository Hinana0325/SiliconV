cmake_minimum_required(VERSION 3.16)
project(SiliconV C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(SV_BUILD_TESTS "Build tests" OFF)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wpedantic")
set(CMAKE_C_FLAGS_DEBUG "-g -O0 -DSV_DEBUG")
set(CMAKE_C_FLAGS_RELEASE "-O2")

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# ── Core Libraries ────────────────────────────────

add_library(sv_gic STATIC core/irq/gic.c)
target_include_directories(sv_gic PUBLIC core/irq)

add_library(sv_psci STATIC core/object/psci.c)
target_include_directories(sv_psci PUBLIC core/object)

add_library(sv_dtb STATIC core/memory/dtb.c)
target_include_directories(sv_dtb PUBLIC core/memory)

add_library(sv_bootimg STATIC core/vm/bootimg.c)
target_include_directories(sv_bootimg PUBLIC core/vm)

add_library(sv_machine STATIC core/vm/machine.c)
target_include_directories(sv_machine PUBLIC
    core/vm core/irq core/memory core/object
    devices/uart devices/transport devices/virtio-blk
    hypervisor/abstraction
)

# ── Device Libraries ──────────────────────────────

add_library(sv_uart STATIC devices/uart/pl011.c)
target_include_directories(sv_uart PUBLIC devices/uart)

add_library(sv_virtio STATIC devices/transport/virtio_mmio.c)
target_include_directories(sv_virtio PUBLIC devices/transport)

add_library(sv_virtio_blk STATIC devices/virtio-blk/virtio_blk.c)
target_include_directories(sv_virtio_blk PUBLIC
    devices/virtio-blk devices/transport
)

# ── Hypervisor ────────────────────────────────────

add_library(sv_hv STATIC hypervisor/abstraction/hv.c)
target_include_directories(sv_hv PUBLIC hypervisor/abstraction)

if(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64" AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_library(sv_hv_kvm STATIC hypervisor/kvm/kvm.c)
    target_link_libraries(sv_hv_kvm PUBLIC sv_hv)
    set(SV_HAS_KVM TRUE)
    message(STATUS "KVM backend enabled (ARM64 Linux)")
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64" AND CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    add_library(sv_hv_hvf STATIC hypervisor/hvf/hvf.c)
    target_link_libraries(sv_hv_hvf PUBLIC sv_hv)
    set(SV_HAS_HVF TRUE)
    message(STATUS "HVF backend enabled (ARM64 macOS)")
else()
    message(STATUS "KVM/HVF backend skipped (host: ${CMAKE_SYSTEM_PROCESSOR})")
endif()

# ── Main Executable ───────────────────────────────

add_executable(siliconv frontend/cli/main.c)
target_link_libraries(siliconv
    sv_machine sv_bootimg sv_gic sv_psci sv_dtb
    sv_uart sv_virtio sv_virtio_blk sv_hv
)
if(SV_HAS_KVM)
    target_link_libraries(siliconv sv_hv_kvm)
    target_compile_definitions(siliconv PRIVATE SV_HAS_KVM=1)
endif()
if(SV_HAS_HVF)
    target_link_libraries(siliconv sv_hv_hvf)
    target_link_libraries(siliconv "-framework Hypervisor")
    target_compile_definitions(siliconv PRIVATE SV_HAS_HVF=1)
endif()

# ── Boot Stub (ARM64 cross-compile) ───────────────

find_program(AARCH64_AS aarch64-linux-gnu-as)
find_program(AARCH64_LD aarch64-linux-gnu-ld)
find_program(AARCH64_OBJCOPY aarch64-linux-gnu-objcopy)

if(AARCH64_AS AND AARCH64_LD AND AARCH64_OBJCOPY)
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/boot_stub.bin
        COMMAND ${AARCH64_AS} -o ${CMAKE_BINARY_DIR}/boot_stub.o ${CMAKE_SOURCE_DIR}/core/vm/boot_stub.S
        COMMAND ${AARCH64_LD} -T ${CMAKE_SOURCE_DIR}/core/vm/boot_stub.ld -o ${CMAKE_BINARY_DIR}/boot_stub.elf ${CMAKE_BINARY_DIR}/boot_stub.o
        COMMAND ${AARCH64_OBJCOPY} -O binary ${CMAKE_BINARY_DIR}/boot_stub.elf ${CMAKE_BINARY_DIR}/boot_stub.bin
        DEPENDS core/vm/boot_stub.S core/vm/boot_stub.ld
        COMMENT "Building ARM64 boot stub"
    )
    add_custom_target(boot_stub ALL DEPENDS ${CMAKE_BINARY_DIR}/boot_stub.bin)
    message(STATUS "Boot stub enabled (ARM64 cross-compiler found)")
else()
    message(STATUS "Boot stub skipped (install gcc-aarch64-linux-gnu)")
endif()

# ── Summary ───────────────────────────────────────

message(STATUS "")
message(STATUS "SiliconV: ${CMAKE_SYSTEM_PROCESSOR} / ${CMAKE_BUILD_TYPE}")
message(STATUS "  KVM: ${SV_HAS_KVM}")
message(STATUS "")
